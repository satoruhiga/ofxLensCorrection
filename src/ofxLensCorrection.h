#pragma once

#include "ofMain.h"

#define OFX_LENS_CORRECTION_BEGIN_NAMESPACE namespace ofx { namespace LensCorrection {
#define OFX_LENS_CORRECTION_END_NAMESPACE } }

OFX_LENS_CORRECTION_BEGIN_NAMESPACE

class Distort
{
public:
	
	Distort(ofFbo::Settings fbo_settings, int width, int height, float camera_matrix[9], float* dist_coeffs, size_t dist_coeffs_size)
	: target_camera(NULL)
	{
		this->width = width;
		this->height = height;
		
		std::copy<float*>(camera_matrix, camera_matrix + 9,
						  this->camera_matrix);
		std::fill_n(this->dist_coeffs, 8, 0.0f);
		std::copy<float*>(dist_coeffs, dist_coeffs + dist_coeffs_size,
						  this->dist_coeffs);
		
		calcFrameBufferSize();
		
		projection_scale = ofVec2f(width, height) / frame_buffer_size;
		
		fbo_settings.width = frame_buffer_size.x;
		fbo_settings.height = frame_buffer_size.y;
		
		fbo.allocate(fbo_settings);
		
		const char *src = "uniform sampler2DRect tex;"
		""
		"uniform vec2 in_size;"
		"uniform vec2 out_size;"
		"uniform float camera_matrix[9];"
		"uniform float dist_coeffs[8];"
		""
		"vec2 size_offset = (in_size - out_size) * 0.5;"
		""
		"float k1 = dist_coeffs[0];"
		"float k2 = dist_coeffs[1];"
		"float p1 = dist_coeffs[2];"
		"float p2 = dist_coeffs[3];"
		"float k3 = dist_coeffs[4];"
		"float k4 = dist_coeffs[5];"
		"float k5 = dist_coeffs[6];"
		"float k6 = dist_coeffs[7];"
		""
		"float cx = camera_matrix[2];"
		"float cy = camera_matrix[5];"
		"float fx = camera_matrix[0];"
		"float fy = camera_matrix[4];"
		""
		"void main()"
		"{"
		"	vec2 out_uv = gl_TexCoord[0].xy;"
		""
		"	float x = (out_uv.x - cx) / fx;"
		"	float y = (out_uv.y - cy) / fy;"
		"	float xy = x * y;"
		"	float x2 = x * x;"
		"	float y2 = y * y;"
		"	float r2 = x2 + y2;"
		"	float r4 = r2 * r2;"
		"	float r6 = r2 * r2 * r2;"
		"	float _2xy = 2.0 * xy;"
		""
		"	float k_radial = (1.0 + k1*r2 + k2*r4 + k3*r6) / (1.0 + k4*r2 + k5*r4 + k6*r6);"
		""
		"	float x_d = x * k_radial + (_2xy * p1 + p2 * (r2 + 2.0 * x2));"
		"	float y_d = y * k_radial + (_2xy * p2 + p1 * (r2 + 2.0 * y2));"
		""
		"	float u = fx * x_d + cx;"
		"	float v = fy * y_d + cy;"
		""
		"	float u_dist = u - out_uv.x;"
		"	float v_dist = v - out_uv.y;"
		""
		"	gl_FragColor = texture2DRect(tex, out_uv + size_offset - vec2(u_dist, v_dist));"
		"}";
		
		shader.setupShaderFromSource(GL_FRAGMENT_SHADER, src);
		shader.linkProgram();
		
		shader.begin();
		shader.setUniform2f("in_size", frame_buffer_size.x, frame_buffer_size.y);
		shader.setUniform2f("out_size", width, height);
		shader.setUniform1fv("camera_matrix", camera_matrix, 9);
		shader.setUniform1fv("dist_coeffs", dist_coeffs, 8);
		shader.end();
	}
	
	void begin()
	{
		fbo.begin();
		
		ofPushView();
		ofViewport(0, 0, fbo.getWidth(), fbo.getHeight());
		
		ofPushMatrix();

		target_camera = NULL;
		
		rescaleProjectionMatrix();
	}
	
	void begin(ofCamera &cam)
	{
		fbo.begin();
		
		ofPushView();
		ofViewport(0, 0, fbo.getWidth(), fbo.getHeight());
		
		ofPushMatrix();
		
		target_camera = &cam;
		target_camera->begin();
		
		rescaleProjectionMatrix();
	}
	
	void end()
	{
		if (target_camera)
		{
			target_camera->end();
			target_camera = NULL;
		}
		
		ofPopMatrix();
		ofPopView();
		
		fbo.end();
	}
	
	void draw(float x, float y, float w = 0, float h = 0)
	{
		if (w == 0) w = width;
		if (h == 0) h = height;
		
		shader.begin();
		shader.setUniformTexture("tex", fbo.getTextureReference(), 1);
		
		ofPushMatrix();
		ofTranslate(x, y);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0);
		glVertex2f(0, 0);
		
		glTexCoord2f(width, 0);
		glVertex2f(w, 0);
		
		glTexCoord2f(width, height);
		glVertex2f(w, h);
		
		glTexCoord2f(0, height);
		glVertex2f(0, h);
		glEnd();
		ofPopMatrix();
		
		shader.end();
	}
	
protected:
	
	int width, height;
	float camera_matrix[9];
	float dist_coeffs[8];
	
	ofShader shader;
	ofFbo fbo;
	
	ofVec2f top_left, bottom_right;
	ofVec2f frame_buffer_size;
	
	ofVec2f projection_scale;
	ofCamera* target_camera;
	
	void rescaleProjectionMatrix()
	{
		ofMatrix4x4 projection;
		glGetFloatv(GL_PROJECTION_MATRIX, projection.getPtr());
		
		projection.scale(projection_scale.x, projection_scale.y, 1);
		
		ofSetMatrixMode(OF_MATRIX_PROJECTION);
		ofLoadMatrix(projection);
		
		ofSetMatrixMode(OF_MATRIX_MODELVIEW);
	}
	
	ofVec2f calcFrameBufferSize()
	{
		ofVec2f UVs[8];
		
		UVs[0] = getUndistortedUV(ofVec2f(0, 0));
		UVs[1] = getUndistortedUV(ofVec2f(width / 2, 0));
		UVs[2] = getUndistortedUV(ofVec2f(width, 0));
		UVs[3] = getUndistortedUV(ofVec2f(0, height / 2));
		UVs[4] = getUndistortedUV(ofVec2f(width, height / 2));
		UVs[5] = getUndistortedUV(ofVec2f(0, height));
		UVs[6] = getUndistortedUV(ofVec2f(width / 2, height));
		UVs[7] = getUndistortedUV(ofVec2f(width, height));
		
		top_left.x = std::numeric_limits<float>::infinity();
		top_left.y = std::numeric_limits<float>::infinity();
		bottom_right.x = -std::numeric_limits<float>::infinity();
		bottom_right.y = -std::numeric_limits<float>::infinity();
		
		for (int i = 0; i < 8; i++)
		{
			top_left.x = min(top_left.x, UVs[i].x);
			top_left.y = min(top_left.y, UVs[i].y);
			bottom_right.x = max(bottom_right.x, UVs[i].x);
			bottom_right.y = max(bottom_right.y, UVs[i].y);
		}
		
		frame_buffer_size = bottom_right - top_left;
	}
	
	ofVec2f getUndistortedUV(const ofVec2f& in_uv)
	{
		float invert = -1;
		
		float k1 = dist_coeffs[0];
		float k2 = dist_coeffs[1];
		float p1 = dist_coeffs[2];
		float p2 = dist_coeffs[3];
		float k3 = dist_coeffs[4];
		float k4 = dist_coeffs[5];
		float k5 = dist_coeffs[6];
		float k6 = dist_coeffs[7];
		
		float cx = camera_matrix[2];
		float cy = camera_matrix[5];
		float fx = camera_matrix[0];
		float fy = camera_matrix[4];
		
		float x = (in_uv.x - cx) / fx;
		float y = (in_uv.y - cy) / fy;
		float xy = x * y;
		float x2 = x * x;
		float y2 = y * y;
		float r2 = x2 + y2;
		float r4 = r2 * r2;
		float r6 = r2 * r2 * r2;
		float _2xy = 2.0 * xy;
		
		float k_radial = (1.0 + k1*r2 + k2*r4 + k3*r6) / (1.0 + k4*r2 + k5*r4 + k6*r6);
		
		float x_d = x * k_radial + (_2xy * p1 + p2 * (r2 + 2.0 * x2));
		float y_d = y * k_radial + (_2xy * p2 + p1 * (r2 + 2.0 * y2));
		
		float u = fx * x_d + cx;
		float v = fy * y_d + cy;
		
		float u_dist = u - in_uv.x;
		float v_dist = v - in_uv.y;
		
		return in_uv + ofVec2f(u_dist, v_dist) * invert;
	}
};

class Undistort
{
public:
	
	Undistort(int width, int height, float camera_matrix[9], float* dist_coeffs, size_t dist_coeffs_size)
	{
		this->width = width;
		this->height = height;
		
		std::copy<float*>(camera_matrix, camera_matrix + 9,
						  this->camera_matrix);
		std::fill_n(this->dist_coeffs, 8, 0.0f);
		std::copy<float*>(dist_coeffs, dist_coeffs + dist_coeffs_size,
						  this->dist_coeffs);
		
		const char* src = "uniform sampler2DRect tex;"
		""
		"uniform float camera_matrix[9];"
		"uniform float dist_coeffs[8];"
		""
		"float k1 = dist_coeffs[0];"
		"float k2 = dist_coeffs[1];"
		"float p1 = dist_coeffs[2];"
		"float p2 = dist_coeffs[3];"
		"float k3 = dist_coeffs[4];"
		"float k4 = dist_coeffs[5];"
		"float k5 = dist_coeffs[6];"
		"float k6 = dist_coeffs[7];"
		""
		"float cx = camera_matrix[2];"
		"float cy = camera_matrix[5];"
		"float fx = camera_matrix[0];"
		"float fy = camera_matrix[4];"
		""
		"void main()"
		"{"
		"	vec2 out_uv = gl_TexCoord[0].xy;"
		""
		"	float x = (out_uv.x - cx) / fx;"
		"	float y = (out_uv.y - cy) / fy;"
		"	float xy = x * y;"
		"	float x2 = x * x;"
		"	float y2 = y * y;"
		"	float r2 = x2 + y2;"
		"	float r4 = r2 * r2;"
		"	float r6 = r2 * r2 * r2;"
		"	float _2xy = 2.0 * xy;"
		""
		"	float k_radial = (1.0 + k1*r2 + k2*r4 + k3*r6) / (1.0 + k4*r2 + k5*r4 + k6*r6);"
		""
		"	float x_d = x * k_radial + (_2xy * p1 + p2 * (r2 + 2.0 * x2));"
		"	float y_d = y * k_radial + (_2xy * p2 + p1 * (r2 + 2.0 * y2));"
		""
		"	float u = fx * x_d + cx;"
		"	float v = fy * y_d + cy;"
		""
		"	gl_FragColor = texture2DRect(tex, vec2(u, v));"
		"}";
		
		shader.setupShaderFromSource(GL_FRAGMENT_SHADER, src);
		shader.linkProgram();
		
		shader.begin();
		shader.setUniform1f("invert", 1);
		shader.setUniform1fv("camera_matrix", camera_matrix, 9);
		shader.setUniform1fv("dist_coeffs", dist_coeffs, 8);
		shader.end();
	}
	
	void begin()
	{
		shader.begin();
	}
	
	void end()
	{
		shader.end();
	}
	
protected:
	
	int width, height;
	float camera_matrix[9];
	float dist_coeffs[8];
	
	ofShader shader;
};

class Parameter
{
public:
	
	bool setup(int width, int height, float camera_matrix[9], float* dist_coeffs, size_t dist_coeffs_size = 6)
	{
		this->width = width;
		this->height = height;
		this->dist_coeffs_size = dist_coeffs_size;
		
		std::copy<float*>(camera_matrix, camera_matrix + 9,
						  this->camera_matrix);
		std::fill_n(this->dist_coeffs, 8, 0.0f);
		std::copy<float*>(dist_coeffs, dist_coeffs + dist_coeffs_size,
						  this->dist_coeffs);
	}
	
	bool setupWithLensXML(const string& path)
	{
		ofXml xml;
		if (!xml.load(path)) return false;
		
		int width = xml.getValue("width", 0);
		int height = xml.getValue("height", 0);
		
		float camera_matrix[9] = {
			xml.getValue("fx", 0.0), xml.getValue("skew", 0.0),
			xml.getValue("cx", 0.0), 0,
			xml.getValue("fy", 0.0), xml.getValue("cy", 0.0),
			0,					   0,
			1};
		
		float dist_coeffs[6] = {xml.getValue("k1", 0.0), xml.getValue("k2", 0.0),
			xml.getValue("p1", 0.0), xml.getValue("p2", 0.0),
			xml.getValue("k3", 0.0), xml.getValue("k4", 0.0)};
		
		return setup(width, height, camera_matrix, dist_coeffs);
	}
	
	ofPtr<Distort> getDistort(ofFbo::Settings fbo_settings = ofFbo::Settings())
	{
		Distort *o = new Distort(fbo_settings, width, height, camera_matrix, dist_coeffs, dist_coeffs_size);
		return ofPtr<Distort>(o);
	}
	
	ofPtr<Undistort> getUndistort()
	{
		Undistort *o = new Undistort(width, height, camera_matrix, dist_coeffs, dist_coeffs_size);
		return ofPtr<Undistort>(o);
	}
	
protected:
	
	int width, height;
	float camera_matrix[9];
	float dist_coeffs[8];
	size_t dist_coeffs_size;
};

OFX_LENS_CORRECTION_END_NAMESPACE

namespace ofxLensCorrection = ofx::LensCorrection;
