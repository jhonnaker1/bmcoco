//
// shader.h
//
// BMA800XL - A bare metal Atari 800XL emulator for Raspberry Pi
// Copyright (C) 2026 aminch <https://github.com/aminch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Modified for BMCoco.
//
// Compiled GPU shaders, as written by tools/v3dshader: QPU code, the list
// of uniforms the code reads, and the facts about each shader that the
// shader state record needs.
//
// The uniform kinds and fields follow Mesa, src/broadcom/compiler/
// v3d_compiler.h (Copyright (C) 2016 Broadcom, MIT licence).
//

#ifndef BMCOCO_V3D_SHADER_H_
#define BMCOCO_V3D_SHADER_H_

#include <stdint.h>

namespace v3d {

// What a uniform holds.
enum UniformKind {
  UniformConstant,        // DATA itself
  UniformParameter,       // 32-bit word DATA of the shader parameters
  UniformViewportXScale,  // half the viewport width, in the clipper's units
  UniformViewportYScale,
  UniformViewportZOffset,
  UniformViewportZScale,
  UniformTmuConfigP0,     // texture state address | DATA (texture DATA >> 24)
  UniformTmuConfigP1,     // sampler state address | DATA
  UniformTextureWidth,    // of texture DATA
  UniformTextureHeight,
};

struct Uniform {
  UniformKind kind;
  uint32_t data;
};

struct Shader {
  const uint64_t *code;
  unsigned instructions;
  const Uniform *uniforms;
  unsigned uniform_count;
  unsigned threads;            // 1, 2 or 4
  bool single_segment;         // starts in the final thread section
  // Vertex and coordinate shaders only.
  unsigned vpm_input_size;
  unsigned vpm_output_size;
  bool separate_segments;
  bool uses_vertex_id;
  bool uses_instance_id;
  unsigned attribute_size;     // values read from vertex attribute 0
};

// VPM set-up for one pass (binning or rendering).
struct Vpm {
  unsigned as, ve, vc;
};

// A fragment shader with the vertex shader that feeds it, and the
// vertex shader's binning (coordinate) version.
struct Program {
  Shader fs, vs, cs;
  unsigned varyings;
  uint32_t flat_shade_flags;
  bool writes_z;
  bool disable_early_z;
  bool uses_centre_w;
  bool scoreboard_wait_on_first_switch;
  bool implicit_point_line_varyings;
  Vpm bin, render;
};

} // namespace v3d

#endif
