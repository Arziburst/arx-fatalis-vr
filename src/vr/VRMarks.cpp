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
#include <array>
#include <atomic>
#include <ctime>
#include <deque>
#include <functional>
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
#include "vr/ReplayRecorder.h"
#include "vr/SceneSubtitles.h"
#include "vr/VoiceNote.h"
#include "vr/VRInput.h"
#include "vr/VRSimulator.h"
#include "vr/VRSystem.h"

namespace vr {
namespace marks {

namespace {

//! How long a mark waits for its eye images (a frame that draws no eye, a session that stops): then without them
const s64 EyeWaitMs = 1000;
//! The replay ring's window in clip.vrsim (#551: the last 30 s, as `replay mark` cuts by default)
const u32 ClipSeconds = 30;

//! A piece of file work for the writer thread, and what the frame thread does once it is done
struct Job {
	std::function<void()> work;
	std::function<void()> done;
};

//! A mark waiting for its eye images before its files are written
struct PendingMark {
	u32 number = 0;
	std::string name;
	fs::path folder;
	s64 since = 0;
	std::string meta;
	std::string clip;
};

struct MarksState {

	MarksStatus status;
	bool noteRead = false; //!< status.note was read from cfg.ini

	std::unique_ptr<PendingMark> pending;

	std::deque<Job> jobs;
	std::thread worker;
	std::atomic<bool> workerDone { false };
	std::function<void()> workerFinished;

	std::unique_ptr<NoteDevice> microphone;
	VoiceNote note;
	u32 noteNumber = 0;     //!< the mark the note belongs to
	fs::path noteFolder;

	s64 markedAt = -1;      //!< when the last mark was made, for its notice
	u32 markedNumber = 0;
	s64 savedAt = -1;       //!< when the last note.wav was written, for "Note saved"
	s64 savedLengthMs = 0;
	bool saving = false;    //!< a kept note was handed to the writer: the indicator stays, frozen, until it is written
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

//! The note that ended (or was stopped) into note.wav of its mark, when it recorded anything
void saveNote() {
	if(!g.note.kept() || g.noteFolder.empty()) {
		LogInfo << "[vr] mark #" << g.noteNumber << ": no voice note recorded";
		return;
	}
	// The samples move to the writer thread, which builds the WAV (up to 9.6 MB): the frame only hands them over
	auto samples = std::make_shared<std::vector<s16>>(g.note.takeSamples());
	const size_t count = samples->size();
	const s64 lengthMs = s64(count) * 1000 / s64(VoiceNote::sampleRate);
	const fs::path folder = g.noteFolder;
	const u32 number = g.noteNumber;
	// The indicator stays, frozen at the note's length, until "Note saved" takes its place
	g.saving = true;
	g.savingLengthMs = lengthMs;
	Job job;
	job.work = [folder, samples]() {
		const std::string wav = formatWav(samples->data(), samples->size(), VoiceNote::sampleRate);
		samples->clear();
		samples->shrink_to_fit();
		fs::create_directories(folder);
		fs::write(folder / "note.wav", wav);
	};
	job.done = [folder, number, count, lengthMs]() {
		std::ostringstream seconds;
		seconds.precision(1);
		seconds << std::fixed << double(count) / double(VoiceNote::sampleRate);
		LogInfo << "[vr] mark #" << number << ": note.wav (" << seconds.str() << " s)";
		if(folder.filename() == g.status.last) {
			g.status.lastFiles = listFiles(folder);
		}
		// "Note saved" once it is on disk, straight after the frozen indicator
		g.saving = false;
		g.savedAt = toMsi(platform::getTime());
		g.savedLengthMs = lengthMs;
	};
	g.jobs.push_back(std::move(job));
}

void writePending(std::array<RgbImage, 2> eyes);

//! The gesture came on: number the mark, take what only this frame has, ask for the eyes, start the note
void startMark(s64 now) {

	if(g.pending) {
		// The eyes of the last mark never came: written without them
		cancelEyeCapture();
		writePending(std::array<RgbImage, 2>());
	}

	const u32 number = ++g.status.count;
	const s64 unixSeconds = s64(std::time(nullptr));
	auto mark = std::make_unique<PendingMark>();
	mark->number = number;
	mark->name = markFolderName(unixSeconds, number);
	mark->folder = fs::getUserDir() / "marks" / mark->name;
	mark->since = now;

	MarkMeta meta;
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
	mark->meta = formatMarkMeta(meta);

	// The replay ring now: the clip ends at this frame
	const std::string error = replay::cutClip(mark->name, ClipSeconds, mark->clip);
	if(!error.empty()) {
		LogWarning << "[vr] mark #" << number << ": no clip.vrsim - " << error;
	}

	g.status.last = mark->name;
	g.status.lastFiles.clear();
	g.markedAt = now;
	g.savedAt = -1;
	g.markedNumber = number;
	LogInfo << "[vr] mark #" << number << ": the moment is marked (both trackpads pressed)";

	// The voice note from the mark on: an earlier one still recording is kept in its own mark
	if(g.note.recording()) {
		g.note.stop();
		saveNote();
	}
	if(g.status.note && !noteMicrophoneAllowed(isSimulated())) {
		LogInfo << "[vr] mark #" << number << ": no voice note - the simulated headset opens no microphone";
	} else if(g.status.note) {
		g.noteNumber = number;
		g.noteFolder = mark->folder;
		if(g.note.start(microphone(), now) == VoiceNote::State::Recording) {
			LogInfo << "[vr] mark #" << number << ": recording a voice note while both trackpads are held (at most "
			        << VoiceNote::maxMs / 1000 << " s)";
		}
	}

	requestEyeCapture();
	g.pending = std::move(mark);
}

//! The eyes came (or will not): queue the mark's files
void writePending(std::array<RgbImage, 2> eyes) {

	std::unique_ptr<PendingMark> mark = std::move(g.pending);
	const fs::path folder = mark->folder;
	const u32 number = mark->number;
	const std::string name = mark->name;

	Job job;
	job.work = [mark = std::shared_ptr<PendingMark>(std::move(mark)),
	            eyes = std::make_shared<std::array<RgbImage, 2>>(std::move(eyes))]() {
		if(!fs::create_directories(mark->folder)) {
			return;
		}
		fs::write(mark->folder / "meta.txt", mark->meta);
		if(!mark->clip.empty()) {
			fs::write(mark->folder / "clip.vrsim", mark->clip);
		}
		const std::string recorder = platform::flightRecorderText();
		if(!recorder.empty()) {
			fs::write(mark->folder / "recorder.txt", recorder);
		}
		const char * const names[2] = { "left.png", "right.png" };
		for(size_t eye = 0; eye < 2; eye++) {
			const std::string png = formatPng((*eyes)[eye]);
			if(!png.empty()) {
				fs::write(mark->folder / names[eye], png);
			}
		}
	};
	job.done = [folder, number, name]() {
		const std::string files = listFiles(folder);
		if(name == g.status.last) {
			g.status.lastFiles = files;
		}
		LogInfo << "[vr] mark #" << number << ": marks\\" << name << " files=" << files;
	};
	g.jobs.push_back(std::move(job));
}

//! The hero-dialogue panel (vr::markNotice): the indicator, "Note saved", the confirmation, or nothing
void updateNotice(s64 now) {
	MarkNoticeState state;
	state.recording = g.note.recording();
	state.recordingMs = state.recording ? now - g.note.startMs() : 0;
	state.saving = g.saving;
	state.savingLengthMs = g.savingLengthMs;
	state.sinceSavedMs = g.savedAt >= 0 ? now - g.savedAt : -1;
	state.savedLengthMs = g.savedLengthMs;
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
		} else {
			startMark(now);
		}
	}

	if(g.pending) {
		std::array<RgbImage, 2> eyes;
		if(takeEyeCapture(eyes)) {
			writePending(std::move(eyes));
		} else if(now - g.pending->since >= EyeWaitMs) {
			LogWarning << "[vr] mark #" << g.pending->number << ": no eye images drawn in " << EyeWaitMs << " ms";
			cancelEyeCapture();
			writePending(std::array<RgbImage, 2>());
		}
	}

	// Push-to-talk: the note records while both trackpads stay down, a release of either ends it
	if(g.note.recording() && g.note.update(now, controls().markHeld()) != VoiceNote::State::Recording) {
		saveNote();
	}
	g.status.recording = g.note.recording();
	g.status.noteMs = g.note.recording() ? std::max<s64>(0, now - g.note.startMs()) : 0;

	updateNotice(now);
	runJobs();
}

void shutdown() {
	if(g.pending) {
		cancelEyeCapture();
		writePending(std::array<RgbImage, 2>());
	}
	if(g.note.recording()) {
		g.note.stop();
		saveNote();
	}
	g.status.recording = false;
	g.status.noteMs = 0;
	finishJobs();
	g.microphone.reset();
}

std::string command(std::string_view rest) {
	std::istringstream iss{std::string(rest)};
	std::string what, value, extra;
	iss >> what >> value;
	if(what != "note" || (value != "on" && value != "off") || (iss >> extra)) {
		return "marks: expected note on|off, got \"" + std::string(rest) + "\"";
	}
	g.noteRead = true;
	g.status.note = (value == "on");
	if(!g.status.note && g.note.recording()) {
		g.note.stop();
		saveNote();
	}
	LogInfo << "[vr] marks: voice note " << value;
	return std::string();
}

} // namespace marks
} // namespace vr
