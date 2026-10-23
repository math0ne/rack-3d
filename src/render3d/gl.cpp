#include <render3d/gl.hpp>


namespace rack {
namespace render3d {


static GLuint compileShader(GLenum type, const char* src) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		WARN("render3d: shader compile failed: %s", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}


GLuint compileProgram(const char* vertSrc, const char* fragSrc) {
	GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
	GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);
	if (!vert || !frag) {
		if (vert)
			glDeleteShader(vert);
		if (frag)
			glDeleteShader(frag);
		return 0;
	}
	GLuint program = glCreateProgram();
	glAttachShader(program, vert);
	glAttachShader(program, frag);
	glLinkProgram(program);
	glDeleteShader(vert);
	glDeleteShader(frag);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		WARN("render3d: program link failed: %s", log);
		glDeleteProgram(program);
		return 0;
	}
	return program;
}


void Mesh::upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
	if (!vao) {
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ibo);
	}
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*) offsetof(Vertex, px));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*) offsetof(Vertex, nx));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*) offsetof(Vertex, u));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	indexCount = indices.size();
}


void Mesh::draw() {
	if (!vao)
		return;
	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, NULL);
	glBindVertexArray(0);
}


void Mesh::destroy() {
	if (vao) {
		glDeleteBuffers(1, &ibo);
		glDeleteBuffers(1, &vbo);
		glDeleteVertexArrays(1, &vao);
	}
	vao = vbo = ibo = 0;
	indexCount = 0;
}


bool RenderTarget::resize(int width, int height, int samples) {
	if (width == this->width && height == this->height && samples == this->samples && fbo)
		return true;
	destroy();
	if (width <= 0 || height <= 0)
		return false;
	this->width = width;
	this->height = height;
	this->samples = samples;

	// Multisampled color + depth
	glGenFramebuffers(1, &msFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
	glGenRenderbuffers(1, &msColor);
	glBindRenderbuffer(GL_RENDERBUFFER, msColor);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
	glGenRenderbuffers(1, &msDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
	bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

	// Resolved texture
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (!ok) {
		WARN("render3d: could not create %dx%d render target with %d samples", width, height, samples);
		destroy();
		return false;
	}
	return true;
}


void RenderTarget::bind() {
	glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
	glViewport(0, 0, width, height);
}


void RenderTarget::resolve() {
	glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void RenderTarget::destroy() {
	if (msFbo)
		glDeleteFramebuffers(1, &msFbo);
	if (msColor)
		glDeleteRenderbuffers(1, &msColor);
	if (msDepth)
		glDeleteRenderbuffers(1, &msDepth);
	if (fbo)
		glDeleteFramebuffers(1, &fbo);
	if (texture)
		glDeleteTextures(1, &texture);
	msFbo = msColor = msDepth = fbo = texture = 0;
	width = height = samples = 0;
}


bool DepthTarget::resize(int size) {
	if (size == this->size && fbo)
		return true;
	destroy();
	if (size <= 0)
		return false;
	this->size = size;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	const float border[4] = {1, 1, 1, 1};
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glBindTexture(GL_TEXTURE_2D, 0);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (!ok) {
		WARN("render3d: could not create %dx%d depth target", size, size);
		destroy();
		return false;
	}
	return true;
}


void DepthTarget::bind() {
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, size, size);
	glClear(GL_DEPTH_BUFFER_BIT);
}


void DepthTarget::destroy() {
	if (fbo)
		glDeleteFramebuffers(1, &fbo);
	if (texture)
		glDeleteTextures(1, &texture);
	fbo = texture = 0;
	size = 0;
}


} // namespace render3d
} // namespace rack
