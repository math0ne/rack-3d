#include <widget/OpenGlWidget.hpp>
#include <context.hpp>


namespace rack {
namespace widget {


void OpenGlWidget::step() {
	// Render every frame
	dirty = true;
	FramebufferWidget::step();
}


static GLuint compileShader(GLenum type, const char* src) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		WARN("OpenGlWidget shader compile failed: %s", log);
	}
	return shader;
}


void OpenGlWidget::drawFramebuffer() {
	math::Vec fbSize = getFramebufferSize();
	glViewport(0.0, 0.0, fbSize.x, fbSize.y);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	// Demo drawing: an RGB triangle covering the lower-left half, in core-profile GL.
	// Resources are shared by all instances and live as long as the GL context.
	static GLuint program = 0;
	static GLuint vao = 0;
	static GLuint vbo = 0;
	if (!program) {
		const char* vertSrc =
			"#version 450 core\n"
			"layout(location = 0) in vec2 pos;\n"
			"layout(location = 1) in vec3 color;\n"
			"out vec3 vColor;\n"
			"void main() { vColor = color; gl_Position = vec4(pos, 0.0, 1.0); }\n";
		const char* fragSrc =
			"#version 450 core\n"
			"in vec3 vColor;\n"
			"out vec4 outColor;\n"
			"void main() { outColor = vec4(vColor, 1.0); }\n";
		GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
		GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);
		program = glCreateProgram();
		glAttachShader(program, vert);
		glAttachShader(program, frag);
		glLinkProgram(program);
		glDeleteShader(vert);
		glDeleteShader(frag);

		// x, y, r, g, b in normalized device coordinates
		const float verts[] = {
			-1.f, -1.f, 1.f, 0.f, 0.f,
			1.f, -1.f, 0.f, 1.f, 0.f,
			-1.f, 1.f, 0.f, 0.f, 1.f,
		};
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*) 0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*) (2 * sizeof(float)));
	}

	glUseProgram(program);
	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	// Leave state as NanoVG expects it
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}


} // namespace widget
} // namespace rack
