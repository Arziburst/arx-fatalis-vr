/*
 * Copyright 2026 Arx Fatalis VR contributors
 *
 * This file is part of Arx Fatalis VR, based on Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "vr/VRMarks.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <zlib.h>

#include "core/Config.h"
#include "core/Core.h"
#include "core/GameEvent.h"
#include "core/Version.h"
#include "game/Player.h"
#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/log/Logger.h"
#include "platform/DumpHelper.h"
#include "platform/Time.h"
#include "vr/EyeImage.h"
#include "vr/MarkMoment.h"
#include "vr/NoteMicrophone.h"
#include "vr/ReplayClip.h"
#include "vr/ReplayRecorder.h"
#include "vr/SceneSubtitles.h"
#include "vr/SnapshotImage.h"
#include "vr/VoiceNote.h"
#include "vr/VRInput.h"
#include "vr/VRSimulator.h"
#include "vr/VRSystem.h"

namespace vr {
namespace marks {

namespace {

//! How long a mark waits for an eye picture it asked for (a frame that draws no eye, a session that stops): then
//! without it
const s64 EyeWaitMs = 1000;

//! A piece of file work for the writer thread, and what the frame thread does once it is done
struct Job {
	std::function<void()> work;
	std::function<void()> done;
};

//! What an eye picture asked for is
struct Shot {
	enum class Kind { Left, Right, Snapshot };
	Kind kind = Kind::Left;
	s64 offsetMs = 0;    //!< a snapshot's time after the press (its name)
	s64 requestedAt = 0; //!< when it was asked for, for EyeWaitMs
};

//! The recording being made (#632: from the start press to the stop press or the cap) until every file of it is
//! queued
struct ActiveMark {
	u32 number = 0;
	std::string name;
	fs::path folder;
	s64 pressAt = 0;          //!< the start press, ms of the platform clock
	s64 pressUs = -1;         //!< ... on the replay clip's clock, -1 before it started
	MarkMeta meta;            //!< the stop and the pose filled in as they are known
	bool poseDue = true;      //!< the start frame's pose is read on the next update (the recorder has it then)
	bool decided = false;     //!< stopped (the second press or the cap): the recording is over
	s64 heldMs = 0;           //!< from the start to the stop
	s64 releaseUs = -1;       //!< the stop on the clip's clock
	bool noteKept = false;    //!< its voice note was kept: note.wav is among its files
	bool clipDue = false;     //!< the clip is cut on the next update (the ring has the stop frame then)
	u32 periodic = 0;         //!< the periodic snapshots asked for
	s64 lastSnapshotMs = -1;  //!< the offset of the last snapshot asked for
	std::map<u32, Shot> shots; //!< the eye pictures asked for and not yet come
};

struct MarksState {

	MarksStatus status;
	bool noteRead = false; //!< status.note was read from cfg.ini

	std::unique_ptr<ActiveMark> mark;
	u32 nextShot = 1;

	std::deque<Job> jobs;
	std::thread worker;
	std::atomic<bool> workerDone { false };
	std::function<void()> workerFinished;

	std::unique_ptr<NoteDevice> microphone;
	VoiceNote note;
	u32 noteNumber = 0;     //!< the mark the note belongs to
	fs::path noteFolder;

	//! The longest recording (#632): HoldMaxMs, the channel's `marks cap <ms>` for the session
	s64 capMs = HoldMaxMs;

	s64 markedAt = -1;      //!< when the last recording started, for its notice
	u32 markedNumber = 0;
	s64 savedAt = -1;       //!< when the last recording's files were written, for "saved"
	s64 savedLengthMs = 0;
	bool savedNote = false; //!< ... with a note.wav
	bool saving = false;    //!< a stopped recording's files are written: the indicator stays, frozen, until they are
	s64 savingLengthMs = 0;
	bool noticeShown = false;

	~MarksState() {
		// An exit that skipped shutdown(): a joinable thread must not be destroyed
		if(worker.joinable()) {
			worker.join();
		}
	}

};

MarksState g;

//! The 4 bytes of \a value, big-endian, after \a out
void appendU32BE(std::string & out, u32 value) {
	out.push_back(char((value >> 24) & 0xff));
	out.push_back(char((value >> 16) & 0xff));
	out.push_back(char((value >> 8) & 0xff));
	out.push_back(char(value & 0xff));
}

void appendPngChunk(std::string & out, const char * type, const std::string & data) {
	appendU32BE(out, u32(data.size()));
	const size_t start = out.size();
	out.append(type, 4);
	out += data;
	const uLong crc = crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(out.data() + start),
	                        uInt(out.size() - start));
	appendU32BE(out, u32(crc));
}

/*!
 * \a image (RGB8, rows bottom-up) as an 8-bit RGB PNG: rows top-down with the Sub filter, deflated fast - an eye
 * image of the Index in a fraction of a second on the writer thread. "" when it cannot be compressed
 */
std::string formatPng(const RgbImage & image) {

	if(image.empty()) {
		return std::string();
	}

	const size_t stride = image.width * 3;
	std::vector<u8> raw((stride + 1) * image.height);
	for(size_t y = 0; y < image.height; y++) {
		const u8 * row = image.at(0, image.height - 1 - y);
		u8 * out = &raw[y * (stride + 1)];
		out[0] = 1; // Sub: each byte less the one of the pixel to its left
		for(size_t i = 0; i < stride; i++) {
			out[1 + i] = u8(row[i] - (i >= 3 ? row[i - 3] : 0));
		}
	}

	uLongf packedSize = compressBound(uLong(raw.size()));
	std::string packed(size_t(packedSize), '\0');
	if(compress2(reinterpret_cast<Bytef *>(packed.data()), &packedSize, raw.data(), uLong(raw.size()), Z_BEST_SPEED)
	   != Z_OK) {
		return std::string();
	}
	packed.resize(size_t(packedSize));

	std::string header;
	appendU32BE(header, u32(image.width));
	appendU32BE(header, u32(image.height));
	header += char(8); // bits per channel
	header += char(2); // RGB
	header += char(0); // deflate
	header += char(0); // adaptive filters
	header += char(0); // no interlace

	std::string png("\x89PNG\r\n\x1a\n", 8);
	appendPngChunk(png, "IHDR", header);
	appendPngChunk(png, "IDAT", packed);
	appendPngChunk(png, "IEND", std::string());
	return png;
}

//! The non-empty files of \a folder, sorted by name, joined by ","
std::string listFiles(const fs::path & folder) {
	std::vector<std::string> names;
	for(fs::directory_iterator it(folder); !it.end(); ++it) {
		if(it.is_regular_file() && fs::file_size(folder / it.name()) > 0) {
			names.push_back(it.name());
		}
	}
	std::sort(names.begin(), names.end());
	std::string list;
	for(const std::string & name : names) {
		list += (list.empty() ? "" : ",") + name;
	}
	return list;
}

//! The length of a clip: its last "@<ms>ms" frame line's time less its first's; 0 without frame lines
s64 clipLengthMs(const std::string & clip) {
	s64 first = -1;
	s64 last = -1;
	size_t pos = 0;
	while(pos < clip.size()) {
		size_t end = clip.find('\n', pos);
		if(end == std::string::npos) {
			end = clip.size();
		}
		if(clip[pos] == '@') {
			s64 ms = 0;
			size_t i = pos + 1;
			while(i < end && clip[i] >= '0' && clip[i] <= '9') {
				ms = ms * 10 + (clip[i] - '0');
				i++;
			}
			if(i > pos + 1) {
				first = first < 0 ? ms : first;
				last = ms;
			}
		}
		pos = end + 1;
	}
	return first >= 0 ? last - first : 0;
}

//! Wait for the writer thread's job and run what follows it on this thread
void joinWorker() {
	g.worker.join();
	if(g.workerFinished) {
		std::function<void()> finished = std::move(g.workerFinished);
		g.workerFinished = nullptr;
		finished();
	}
}

//! Start the next queued job when the writer is free; finish the one that is done
void runJobs() {
	if(g.worker.joinable() && g.workerDone.load()) {
		joinWorker();
	}
	if(!g.worker.joinable() && !g.jobs.empty()) {
		Job job = std::move(g.jobs.front());
		g.jobs.pop_front();
		g.workerDone = false;
		g.workerFinished = std::move(job.done);
		g.worker = std::thread([work = std::move(job.work)]() {
			platform::flightNameThread("mark writer");
			work();
			g.workerDone = true;
		});
	}
}

//! Wait for every queued job (the game quits)
void finishJobs() {
	while(g.worker.joinable() || !g.jobs.empty()) {
		if(g.worker.joinable()) {
			joinWorker();
		}
		runJobs();
	}
}

NoteDevice & microphone() {
	if(!g.microphone) {
		g.microphone = defaultMicrophone();
	}
	return *g.microphone;
}

//! A file of \a folder written on the writer thread
void queueFile(const fs::path & folder, std::string name, std::function<std::string()> make,
               std::function<void()> done = nullptr) {
	Job job;
	job.work = [folder, name = std::move(name), make = std::move(make)]() {
		const std::string bytes = make();
		if(!bytes.empty() && fs::create_directories(folder)) {
			fs::write(folder / name, bytes);
		}
	};
	job.done = std::move(done);
	g.jobs.push_back(std::move(job));
}

//! The note that ended (or was stopped) into note.wav of its mark, when it recorded anything; true when one is
//! written
bool saveNote() {
	if(!g.note.kept() || g.noteFolder.empty()) {
		LogInfo << "[vr] mark #" << g.noteNumber << ": no voice note recorded";
		return false;
	}
	// The samples move to the writer thread, which builds the WAV (up to 1.92 MB): the frame only hands them over
	auto samples = std::make_shared<std::vector<s16>>(g.note.takeSamples());
	const size_t count = samples->size();
	const u32 number = g.noteNumber;
	queueFile(g.noteFolder, "note.wav", [samples]() {
		std::string wav = formatWav(samples->data(), samples->size(), VoiceNote::sampleRate);
		samples->clear();
		samples->shrink_to_fit();
		return wav;
	}, [number, count]() {
		std::ostringstream seconds;
		seconds.precision(1);
		seconds << std::fixed << double(count) / double(VoiceNote::sampleRate);
		LogInfo << "[vr] mark #" << number << ": note.wav (" << seconds.str() << " s)";
	});
	return true;
}

//! The note stopped (or ended by itself) and saved: the recording it belongs to has a note.wav when one was kept
void keepNote() {
	g.note.stop();
	if(saveNote() && g.mark && g.mark->number == g.noteNumber) {
		g.mark->noteKept = true;
	}
}

//! Ask for an eye picture of the mark; false (logged) when it cannot be taken
bool requestShot(ActiveMark & mark, Shot shot, size_t eye, bool half, s64 now) {
	const u32 id = g.nextShot++;
	if(!requestEyeShot(id, eye, half)) {
		LogWarning << "[vr] mark #" << mark.number << ": no picture of the " << (eye == 0 ? "left" : "right")
		           << " eye (no session, or every capture slot busy)";
		return false;
	}
	shot.requestedAt = now;
	mark.shots[id] = shot;
	return true;
}

//! A hold's snapshot at \a offsetMs after the press: the left eye, halved
void requestSnapshot(ActiveMark & mark, s64 offsetMs, s64 now) {
	Shot shot;
	shot.kind = Shot::Kind::Snapshot;
	shot.offsetMs = offsetMs;
	if(requestShot(mark, shot, 0, true, now)) {
		mark.lastSnapshotMs = offsetMs;
	}
}

//! An eye picture came back: written by the writer thread
void writeShot(const ActiveMark & mark, const Shot & shot, RgbImage image) {
	auto picture = std::make_shared<RgbImage>(std::move(image));
	if(shot.kind == Shot::Kind::Snapshot) {
		const std::string name = mark.name;
		queueFile(mark.folder, snapshotName(shot.offsetMs), [picture]() { return formatJpeg(*picture); }, [name]() {
			if(name == g.status.last) {
				g.status.lastSnapshots++;
			}
		});
	} else {
		queueFile(mark.folder, shot.kind == Shot::Kind::Left ? "left.png" : "right.png",
		          [picture]() { return formatPng(*picture); });
	}
}

//! The stop: the second press of both trackpads, the cap (\a capped), a new recording or the game quitting. Every
//! recording is a hold of #574 (its folder and meta.txt as they were)
void decide(ActiveMark & mark, s64 now, bool capped) {

	mark.decided = true;
	mark.heldMs = holdLengthMs(capped ? g.capMs : now - mark.pressAt);
	mark.meta.kind = MarkKind::Hold;
	mark.releaseUs = replay::clockNowUs();
	mark.meta.releaseMs = mark.releaseUs >= 0 ? (mark.releaseUs + 500) / 1000 : -1;
	g.status.lastKind = markKindName(MarkKind::Hold);

	if(g.note.recording() && g.noteNumber == mark.number) {
		keepNote();
	}

	LogInfo << "[vr] mark #" << mark.number << ": the recording stopped after " << mark.heldMs << " ms"
	        << (capped ? " - its cap, by itself" : "");
	// The indicator stays, frozen at the recording's length, until "saved" takes its place
	g.saving = true;
	g.savingLengthMs = mark.heldMs;
	// The stop's snapshot, unless the last periodic one has its name
	if(mark.lastSnapshotMs < 0 || snapshotName(mark.lastSnapshotMs) != snapshotName(mark.heldMs)) {
		requestSnapshot(mark, mark.heldMs, now);
	}
	mark.clipDue = true;
}

//! The hold's clip: the ring copied now, cut on the writer thread
void cutClip(ActiveMark & mark) {
	mark.clipDue = false;
	auto image = std::make_shared<std::vector<unsigned char>>();
	if(mark.pressUs < 0 || mark.releaseUs < 0 || !replay::copyRing(*image)) {
		LogWarning << "[vr] mark #" << mark.number << ": no clip.vrsim - nothing records (a VR session without "
		           << "--no-replay-log, once the game is playable)";
		return;
	}
	auto clipMs = std::make_shared<s64>(0);
	const std::string name = mark.name;
	const u64 pressUs = u64(mark.pressUs);
	const u64 releaseUs = u64(mark.releaseUs);
	queueFile(mark.folder, "clip.vrsim", [image, clipMs, name, pressUs, releaseUs]() {
		std::string clip = replayRingHoldClip(image->data(), image->size(), name, pressUs, releaseUs,
		                                      u64(HoldContextMs) * 1000);
		image->clear();
		image->shrink_to_fit();
		*clipMs = clipLengthMs(clip);
		return clip;
	}, [name, clipMs]() {
		if(name == g.status.last) {
			g.status.lastClipMs = *clipMs;
		}
	});
}

//! Every file of the mark is queued: meta.txt last, then the folder's listing and the log line
void finish(std::unique_ptr<ActiveMark> mark) {
	const fs::path folder = mark->folder;
	const u32 number = mark->number;
	const std::string name = mark->name;
	const std::string meta = formatMarkMeta(mark->meta);
	const s64 lengthMs = mark->heldMs;
	const bool noteKept = mark->noteKept;
	queueFile(folder, "meta.txt", [meta]() { return meta; }, [folder, number, name, lengthMs, noteKept]() {
		const std::string files = listFiles(folder);
		LogInfo << "[vr] mark #" << number << ": marks\\" << name << " files=" << files;
		if(name == g.status.last) {
			g.status.lastFiles = files;
			// "saved" once every file is on disk, straight after the frozen indicator
			g.saving = false;
			g.savedAt = toMsi(platform::getTime());
			g.savedLengthMs = lengthMs;
			g.savedNote = noteKept;
		}
	});
}

//! Give up on the pictures the mark still waits for (the eyes were never drawn, a new mark, the game quits)
void dropShots(ActiveMark & mark, const char * why) {
	if(!mark.shots.empty()) {
		LogWarning << "[vr] mark #" << mark.number << ": " << mark.shots.size() << " eye picture(s) not taken - " << why;
		mark.shots.clear();
		cancelEyeShots();
	}
}

//! The gesture came on with no recording on: number the recording, take what only this frame has, ask for the eyes,
//! start the note
void startMark(s64 now) {

	if(g.mark) {
		// The last recording is still waiting for something: it is finished with what it has
		if(!g.mark->decided) {
			decide(*g.mark, now, false);
		}
		if(g.mark->clipDue) {
			cutClip(*g.mark);
		}
		dropShots(*g.mark, "a new mark");
		finish(std::move(g.mark));
	}

	const u32 number = ++g.status.count;
	const s64 unixSeconds = s64(std::time(nullptr));
	auto mark = std::make_unique<ActiveMark>();
	mark->number = number;
	mark->name = markFolderName(unixSeconds, number);
	mark->folder = fs::getUserDir() / "marks" / mark->name;
	mark->pressAt = now;
	mark->pressUs = replay::clockNowUs();

	MarkMeta & meta = mark->meta;
	meta.number = number;
	meta.unixSeconds = unixSeconds;
	meta.area = g_currentArea ? u32(g_currentArea) : 0u;
	if(g_currentArea) {
		meta.position = player.pos;
		meta.yaw = player.angle.getYaw();
	}
	meta.build = arx_name + " " + arx_version + " " + arx_git_commit + " " + arx_build_type;
	meta.state = gameevent::gameStateName(gameevent::currentGameState());
	meta.session = sessionStateString();
	meta.pressMs = mark->pressUs >= 0 ? (mark->pressUs + 500) / 1000 : -1;

	g.status.last = mark->name;
	g.status.lastFiles.clear();
	g.status.lastKind = "pending";
	g.status.lastSnapshots = 0;
	g.status.lastClipMs = 0;
	g.markedAt = now;
	g.savedAt = -1;
	g.saving = false;
	g.markedNumber = number;
	LogInfo << "[vr] mark #" << number << ": a recording starts (both trackpads pressed; pressed again it stops, at "
	        << "most " << g.capMs << " ms)";

	// Both eyes of this frame, full size; the recorder's tail as it is now
	Shot left;
	left.kind = Shot::Kind::Left;
	requestShot(*mark, left, 0, false, now);
	Shot right;
	right.kind = Shot::Kind::Right;
	requestShot(*mark, right, 1, false, now);
	queueFile(mark->folder, "recorder.txt", []() { return platform::flightRecorderText(); });

	// The voice note for as long as the recording is on. One still recording is kept in its own mark
	if(g.note.recording()) {
		g.note.stop();
		saveNote();
	}
	if(g.status.note && !noteMicrophoneAllowed(isSimulated())) {
		LogInfo << "[vr] mark #" << number << ": no voice note - the simulated headset opens no microphone";
	} else if(g.status.note) {
		g.noteNumber = number;
		g.noteFolder = mark->folder;
		// Opening the device is the one call on the frame that may take a while: its time is logged
		const PlatformInstant start = platform::getTime();
		const VoiceNote::State state = g.note.start(microphone(), now);
		const s64 openUs = toUs(platform::getTime() - start);
		if(state == VoiceNote::State::Recording) {
			LogInfo << "[vr] mark #" << number << ": recording a voice note until the recording stops (at most "
			        << VoiceNote::maxMs / 1000 << " s; the microphone opened in " << openUs / 1000 << "." << (openUs / 100) % 10
			        << " ms)";
		}
	}

	g.mark = std::move(mark);
}

//! A frame of the recording being made: its pose, the snapshots, the cap, its pictures, its end
void updateMark(s64 now) {

	ActiveMark & mark = *g.mark;

	if(mark.poseDue) {
		// The recorder recorded the press frame after the marks ran on it: its rig is the press frame's
		mark.poseDue = false;
		mark.meta.posed = replay::lastRig(mark.meta.pose);
	}
	if(mark.clipDue) {
		cutClip(mark);
	}

	if(!mark.decided) {
		const s64 held = now - mark.pressAt;
		if(held >= g.capMs) {
			decide(mark, now, true);
		} else {
			// The next periodic snapshot on the first frame at or after its time
			const s64 next = s64(mark.periodic + 1) * SnapshotIntervalMs;
			if(held >= next && next < g.capMs && mark.periodic + 1 < SnapshotMax) {
				mark.periodic++;
				requestSnapshot(mark, next, now);
			}
		}
	}

	// A picture that came back: one a frame, so the copies of the press's two full eyes fall on two frames
	u32 id = 0;
	RgbImage image;
	if(takeEyeShot(id, image)) {
		auto shot = mark.shots.find(id);
		if(shot != mark.shots.end()) {
			writeShot(mark, shot->second, std::move(image));
			mark.shots.erase(shot);
		}
	}
	for(auto shot = mark.shots.begin(); shot != mark.shots.end();) {
		if(now - shot->second.requestedAt >= EyeWaitMs) {
			LogWarning << "[vr] mark #" << mark.number << ": an eye picture was not drawn in " << EyeWaitMs << " ms";
			shot = mark.shots.erase(shot);
			if(mark.shots.empty()) {
				cancelEyeShots();
			}
		} else {
			++shot;
		}
	}

	if(mark.decided && !mark.clipDue && mark.shots.empty()) {
		finish(std::move(g.mark));
	}
}

//! The hero-dialogue panel (vr::markNotice): the indicator, "saved", the confirmation, or nothing
void updateNotice(s64 now) {
	MarkNoticeState state;
	state.recording = g.status.recording;
	state.recordingMs = g.status.recordingMs;
	state.saving = g.saving;
	state.savingLengthMs = g.savingLengthMs;
	state.sinceSavedMs = g.savedAt >= 0 ? now - g.savedAt : -1;
	state.savedLengthMs = g.savedLengthMs;
	state.savedNote = g.savedNote;
	state.sinceMarkedMs = g.markedAt >= 0 ? now - g.markedAt : -1;
	state.markedNumber = g.markedNumber;
	std::string text;
	switch(markNotice(state, text)) {
		case MarkNotice::Recording: showSubtitleNotice(SubtitleNotice::Recording, text); break;
		case MarkNotice::Saved: showSubtitleNotice(SubtitleNotice::Saved, text); break;
		case MarkNotice::Marked: showSubtitleNotice(SubtitleNotice::Marked, text); break;
		case MarkNotice::None: {
			if(g.noticeShown) {
				clearSubtitleNotice();
			}
			g.noticeShown = false;
			return;
		}
	}
	g.noticeShown = true;
}

//! The voice note's setting from cfg.ini, once (the channel's `marks note` replaces it for the session)
void readNoteSetting() {
	if(!g.noteRead) {
		g.noteRead = true;
		g.status.note = config.video.vrMarkNote;
	}
}

} // anonymous namespace

const MarksStatus & status() {
	readNoteSetting();
	return g.status;
}

void update() {

	readNoteSetting();
	if(!isEnabled()) {
		return;
	}

	const s64 now = toMsi(platform::getTime());
	if(controls().markGesture() && markGestureAvailable(controllerType())) {
		const SimStatus sim = isSimulated() ? simStatus() : SimStatus();
		if(!markAllowed(sim.playing && !sim.play.done)) {
			// A replay replays the recorded trackpad presses: the marks were the recorded session's
			LogInfo << "[vr] mark ignored: a replay is playing";
		} else if(g.mark && !g.mark->decided) {
			// A toggle (#632): the same press again stops the recording
			decide(*g.mark, now, false);
		} else {
			startMark(now);
		}
	} else if(g.mark) {
		updateMark(now);
	}

	// The note records for as long as the recording is on (stopped with it, above). Ended by itself (its minute),
	// it is the recording's note
	if(g.note.recording() && g.note.update(now, true) != VoiceNote::State::Recording) {
		keepNote();
	}
	const bool recording = g.mark && !g.mark->decided;
	g.status.recording = recording;
	g.status.recordingMs = recording ? std::max<s64>(0, now - g.mark->pressAt) : 0;
	g.status.capMs = g.capMs;
	g.status.noteMs = g.note.recording() ? std::max<s64>(0, now - g.note.startMs()) : 0;

	updateNotice(now);
	runJobs();
}

void shutdown() {
	if(g.mark) {
		const s64 now = toMsi(platform::getTime());
		if(!g.mark->decided) {
			decide(*g.mark, now, false);
		}
		if(g.mark->clipDue) {
			cutClip(*g.mark);
		}
		dropShots(*g.mark, "the game quits");
		finish(std::move(g.mark));
	}
	if(g.note.recording()) {
		g.note.stop();
		saveNote();
	}
	g.status.recording = false;
	g.status.recordingMs = 0;
	g.status.noteMs = 0;
	finishJobs();
	g.microphone.reset();
}

std::string command(std::string_view rest) {
	std::istringstream iss{std::string(rest)};
	std::string what, value, extra;
	iss >> what >> value;
	const std::string refused = "marks: expected note on|off or cap <" + std::to_string(RecordingCapMinMs) + ".."
	                            + std::to_string(HoldMaxMs) + " ms>, got \"" + std::string(rest) + "\"";
	if(iss >> extra) {
		return refused;
	}
	if(what == "cap") {
		// The test hook of the 1 minute cap (#632): a whole number of ms
		if(value.empty() || value.size() > 6 || value.find_first_not_of("0123456789") != std::string::npos) {
			return refused;
		}
		const s64 ms = std::stoll(value);
		if(ms < RecordingCapMinMs || ms > HoldMaxMs) {
			return refused;
		}
		g.capMs = ms;
		g.status.capMs = ms;
		LogInfo << "[vr] marks: a recording stops by itself after " << ms << " ms";
		return std::string();
	}
	if(what != "note" || (value != "on" && value != "off")) {
		return refused;
	}
	g.noteRead = true;
	g.status.note = (value == "on");
	if(!g.status.note && g.note.recording()) {
		keepNote();
	}
	LogInfo << "[vr] marks: voice note " << value;
	return std::string();
}

} // namespace marks
} // namespace vr
