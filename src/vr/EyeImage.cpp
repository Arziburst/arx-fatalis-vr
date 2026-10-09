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

#include "vr/EyeImage.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace vr {

namespace {

float luma(const u8 * rgb) {
	return 0.2126f * float(rgb[0]) + 0.7152f * float(rgb[1]) + 0.0722f * float(rgb[2]);
}

size_t toPixel(float fraction, size_t size) {
	float clamped = std::clamp(fraction, 0.f, 1.f);
	return std::min(size, size_t(std::lround(clamped * float(size))));
}

} // anonymous namespace

const char * imageVerdictName(ImageVerdict verdict) {
	switch(verdict) {
		case ImageVerdict::Empty: return "empty";
		case ImageVerdict::World: return "world";
		case ImageVerdict::Black: return "black";
		case ImageVerdict::Background: return "background";
		case ImageVerdict::Uniform: return "uniform";
	}
	return "empty";
}

PixelStats pixelStats(const RgbImage & image, const ImageRegion & region, const u8 background[3]) {

	PixelStats stats;
	if(image.empty()) {
		return stats;
	}

	const size_t x0 = toPixel(region.x0, image.width), x1 = toPixel(region.x1, image.width);
	const size_t y0 = toPixel(region.y0, image.height), y1 = toPixel(region.y1, image.height);
	if(x1 <= x0 || y1 <= y0) {
		return stats;
	}

	double sum = 0.0;
	size_t black = 0, matching = 0;
	for(size_t y = y0; y < y1; y++) {
		for(size_t x = x0; x < x1; x++) {
			const u8 * rgb = image.at(x, y);
			const float l = luma(rgb);
			sum += l;
			if(l <= BlackLuma) {
				black++;
			}
			if(std::abs(int(rgb[0]) - int(background[0])) <= BackgroundTolerance
			   && std::abs(int(rgb[1]) - int(background[1])) <= BackgroundTolerance
			   && std::abs(int(rgb[2]) - int(background[2])) <= BackgroundTolerance) {
				matching++;
			}
		}
	}

	const double count = double((x1 - x0) * (y1 - y0));
	const double mean = sum / count;
	// A second pass for the spread: sum of squares minus the squared mean cancels badly
	double squares = 0.0;
	for(size_t y = y0; y < y1; y++) {
		for(size_t x = x0; x < x1; x++) {
			const double d = double(luma(image.at(x, y))) - mean;
			squares += d * d;
		}
	}
	stats.mean = float(mean);
	stats.stddev = float(std::sqrt(squares / count));
	stats.black = float(double(black) / count);
	stats.background = float(double(matching) / count);

	if(stats.black >= VerdictFraction) {
		stats.verdict = ImageVerdict::Black;
	} else if(stats.background >= VerdictFraction) {
		stats.verdict = ImageVerdict::Background;
	} else if(stats.stddev < UniformStddev) {
		stats.verdict = ImageVerdict::Uniform;
	} else {
		stats.verdict = ImageVerdict::World;
	}

	return stats;
}

ImageRegion forwardRegion(const MirrorFov & fov, float half) {
	const Vec2f forward = mirrorForwardPoint(fov);
	ImageRegion region;
	region.x0 = std::clamp(forward.x - half, 0.f, 1.f);
	region.x1 = std::clamp(forward.x + half, 0.f, 1.f);
	region.y0 = std::clamp(forward.y - half, 0.f, 1.f);
	region.y1 = std::clamp(forward.y + half, 0.f, 1.f);
	return region;
}

LumaGrid lumaGrid(const RgbImage & image, size_t width) {

	LumaGrid grid;
	if(image.empty() || width == 0) {
		return grid;
	}

	grid.width = std::min(width, image.width);
	grid.height = std::max(size_t(1), size_t(std::lround(double(image.height) * double(grid.width) / double(image.width))));
	grid.height = std::min(grid.height, image.height);
	grid.values.assign(grid.width * grid.height, 0.f);

	for(size_t gy = 0; gy < grid.height; gy++) {
		const size_t y0 = gy * image.height / grid.height, y1 = (gy + 1) * image.height / grid.height;
		for(size_t gx = 0; gx < grid.width; gx++) {
			const size_t x0 = gx * image.width / grid.width, x1 = (gx + 1) * image.width / grid.width;
			double sum = 0.0;
			for(size_t y = y0; y < y1; y++) {
				for(size_t x = x0; x < x1; x++) {
					sum += luma(image.at(x, y));
				}
			}
			grid.values[gy * grid.width + gx] = float(sum / double((x1 - x0) * (y1 - y0)));
		}
	}

	return grid;
}

float meanAbsDiff(const LumaGrid & a, const LumaGrid & b) {
	if(a.width != b.width || a.height != b.height || a.values.empty()) {
		return -1.f;
	}
	double sum = 0.0;
	for(size_t i = 0; i < a.values.size(); i++) {
		sum += std::abs(double(a.values[i]) - double(b.values[i]));
	}
	return float(sum / double(a.values.size()));
}

ShiftEstimate horizontalShift(const LumaGrid & left, const LumaGrid & right, int maxShift) {

	ShiftEstimate best;
	if(left.width != right.width || left.height != right.height || left.values.empty()) {
		return best;
	}

	const int width = int(left.width);
	maxShift = std::clamp(maxShift, 0, width / 2);
	double bestDiff = std::numeric_limits<double>::max();

	for(int s = -maxShift; s <= maxShift; s++) {
		// left(x) against right(x - s), on the columns both have
		const int x0 = std::max(0, s), x1 = std::min(width, width + s);
		double sum = 0.0;
		for(size_t y = 0; y < left.height; y++) {
			const float * l = &left.values[y * left.width];
			const float * r = &right.values[y * right.width];
			for(int x = x0; x < x1; x++) {
				sum += std::abs(double(l[x]) - double(r[x - s]));
			}
		}
		const double diff = sum / double(size_t(x1 - x0) * left.height);
		if(s == 0) {
			best.unshifted = float(diff);
		}
		// Ties go to the smaller shift
		if(diff < bestDiff - 1e-9 || (std::abs(diff - bestDiff) <= 1e-9 && std::abs(s) < std::abs(best.cells))) {
			bestDiff = diff;
			best.cells = s;
		}
	}

	best.residual = float(bestDiff);
	best.fraction = float(best.cells) / float(width);
	return best;
}

size_t composeQuad(RgbImage & eye, const MirrorFov & fov, const Vec3f & eyePosition, const glm::quat & eyeOrientation,
                   const RgbImage & quad, const QuadPlacement & placement) {

	if(eye.empty() || quad.empty() || placement.width <= 0.f || placement.height <= 0.f) {
		return 0;
	}

	const float tanLeft = std::tan(fov.left), tanRight = std::tan(fov.right);
	const float tanDown = std::tan(fov.down), tanUp = std::tan(fov.up);
	const Vec3f normal = placement.orientation * Vec3f(0.f, 0.f, 1.f);
	const glm::quat toQuad = glm::inverse(placement.orientation);
	const float towards = glm::dot(placement.position - eyePosition, normal);

	size_t covered = 0;
	for(size_t y = 0; y < eye.height; y++) {
		const float ty = tanDown + (float(y) + 0.5f) / float(eye.height) * (tanUp - tanDown);
		for(size_t x = 0; x < eye.width; x++) {
			const float tx = tanLeft + (float(x) + 0.5f) / float(eye.width) * (tanRight - tanLeft);
			const Vec3f direction = eyeOrientation * Vec3f(tx, ty, -1.f);
			const float along = glm::dot(direction, normal);
			if(std::abs(along) < 1e-6f) {
				continue;
			}
			const float t = towards / along;
			if(t <= 0.f) {
				continue;
			}
			const Vec3f local = toQuad * (eyePosition + direction * t - placement.position);
			const float u = local.x / placement.width + 0.5f;
			const float v = local.y / placement.height + 0.5f;
			if(u < 0.f || u >= 1.f || v < 0.f || v >= 1.f) {
				continue;
			}
			const size_t qx = std::min(quad.width - 1, size_t(u * float(quad.width)));
			const size_t qy = std::min(quad.height - 1, size_t(v * float(quad.height)));
			const u8 * source = quad.at(qx, qy);
			u8 * target = eye.at(x, y);
			target[0] = source[0];
			target[1] = source[1];
			target[2] = source[2];
			covered++;
		}
	}

	return covered;
}

} // namespace vr
