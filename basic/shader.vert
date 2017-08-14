#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec4 inColor;

layout (binding = 0) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 projection;
} uMVP;

layout(location = 0) out vec4 outColor;

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  gl_Position = uMVP.projection *  uMVP.view * uMVP.model *
      vec4(inPosition.xyz, 1.0);
  outColor = inColor;
}
