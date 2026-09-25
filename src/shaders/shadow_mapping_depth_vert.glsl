  #version 460 core
    layout (location = 0) in vec3 aPos;

    uniform mat4 ModelMatrix;
    uniform mat4 ViewMatrix;        // ← 这里传的是"灯光的 View"
    uniform mat4 ProjectionMatrix;  // ← 这里传的是"灯光的 Projection"

    void main() {
        gl_Position = ProjectionMatrix * ViewMatrix * ModelMatrix * vec4(aPos, 1.0);
    }