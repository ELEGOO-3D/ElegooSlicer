#include "libslic3r/libslic3r.h"

#include "GPUColorPicker.hpp"

#include "3DScene.hpp"
#include "Camera.hpp"
#include "GUI_App.hpp"
#include "GLShader.hpp"
#include "OpenGLManager.hpp"

#include <glad/gl.h>
#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace GUI {

GPUColorPicker::GPUColorPicker() = default;

GPUColorPicker::~GPUColorPicker()
{
    shutdown();
}

bool GPUColorPicker::init(int width, int height)
{
    if (width <= 0 || height <= 0) {
        BOOST_LOG_TRIVIAL(error) << u8"gpu color picker init failed: invalid viewport size " << width << "x" << height;
        return false;
    }

    if (!OpenGLManager::are_framebuffers_supported()) {
        BOOST_LOG_TRIVIAL(debug) << u8"gpu color picker init skipped: framebuffer extension not ready or unsupported";
        return false;
    }

    if (is_initialized())
        shutdown();

    m_width = width;
    m_height = height;

    return create_framebuffer();
}

void GPUColorPicker::shutdown()
{
    destroy_framebuffer();
    m_width = 0;
    m_height = 0;
}

void GPUColorPicker::resize(int width, int height)
{
    if (width == m_width && height == m_height)
        return;

    if (!OpenGLManager::are_framebuffers_supported())
        return;

    init(width, height);
}

bool GPUColorPicker::create_framebuffer()
{
    glsafe(::glGenFramebuffers(1, &m_fbo));
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));

    glsafe(::glGenTextures(1, &m_pick_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_pick_texture));

    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr));

    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pick_texture, 0));

    glsafe(::glGenRenderbuffers(1, &m_depth_rbo));
    glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, m_depth_rbo));
    glsafe(::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, m_width, m_height));

    glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth_rbo));

    bool success = check_framebuffer_status();

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, 0));

    if (success) {
        BOOST_LOG_TRIVIAL(info) << u8"gpu color picker initialized: " << m_width << "x" << m_height;
    } else {
        BOOST_LOG_TRIVIAL(error) << u8"gpu color picker init failed: framebuffer incomplete";
        destroy_framebuffer();
    }

    return success;
}

void GPUColorPicker::destroy_framebuffer()
{
    if (m_pick_texture != 0) {
        glsafe(::glDeleteTextures(1, &m_pick_texture));
        m_pick_texture = 0;
    }

    if (m_depth_rbo != 0) {
        glsafe(::glDeleteRenderbuffers(1, &m_depth_rbo));
        m_depth_rbo = 0;
    }

    if (m_fbo != 0) {
        glsafe(::glDeleteFramebuffers(1, &m_fbo));
        m_fbo = 0;
    }
}

bool GPUColorPicker::check_framebuffer_status() const
{
    GLenum status = ::glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::string error_msg = "GPUColorPicker FBO status error: ";
        switch (status) {
        case GL_FRAMEBUFFER_UNDEFINED:
            error_msg += "GL_FRAMEBUFFER_UNDEFINED";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            error_msg += "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            error_msg += "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            error_msg += "GL_FRAMEBUFFER_UNSUPPORTED";
            break;
        default:
            error_msg += "Unknown error (0x" + std::to_string(status) + ")";
            break;
        }
        BOOST_LOG_TRIVIAL(error) << u8"gpu color picker framebuffer error: " << error_msg;
        return false;
    }

    return true;
}

void GPUColorPicker::render_for_picking(const std::vector<Slic3r::GLVolume*>& volumes, const Camera& camera)
{
    if (!is_initialized()) {
        BOOST_LOG_TRIVIAL(warning) << u8"gpu color picker render skipped: system not initialized";
        return;
    }

    GLShaderProgram* shader = wxGetApp().get_shader("picking");
    if (shader == nullptr) {
        BOOST_LOG_TRIVIAL(error) << u8"gpu color picker render failed: picking shader unavailable";
        return;
    }

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));
    glsafe(::glViewport(0, 0, m_width, m_height));

    glsafe(::glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthFunc(GL_LESS));

    glsafe(::glDisable(GL_BLEND));

    shader->start_using();

    const Transform3d& view_matrix = camera.get_view_matrix();
    const Transform3d& projection_matrix = camera.get_projection_matrix();

    for (size_t i = 0; i < volumes.size(); ++i) {
        Slic3r::GLVolume* volume = volumes[i];
        // if (!volume->is_wipe_tower)
        //     continue;
        if (!volume->model.is_initialized())
            continue;

        int object_id = static_cast<int>(i) + 1;
        ColorRGBA id_color = id_to_color(object_id);

        Transform3d model_matrix = volume->world_matrix();
        Transform3d view_model_matrix = view_matrix * model_matrix;

        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);
        shader->set_uniform("object_id_color", Vec3f(id_color.r(), id_color.g(), id_color.b()));

        volume->model.render();
    }

    shader->stop_using();
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

int GPUColorPicker::pick_object_id(int mouse_x, int mouse_y)
{
    if (!is_initialized())
        return -1;

    int gl_y = m_height - mouse_y - 1;

    if (mouse_x < 0 || mouse_x >= m_width || gl_y < 0 || gl_y >= m_height)
        return -1;

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));

    unsigned char pixel[3] = { 0, 0, 0 };
    glsafe(::glReadPixels(mouse_x, gl_y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel));

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));

    int object_id = color_to_id(pixel[0], pixel[1], pixel[2]);

    if (object_id == 0)
        return -1;

    return object_id - 1;
}

bool GPUColorPicker::pick_detailed(int mouse_x, int mouse_y, int& out_volume_idx, Vec3f& out_world_pos, float* out_depth)
{
    out_volume_idx = pick_object_id(mouse_x, mouse_y);

    if (out_volume_idx < 0)
        return false;

    int gl_y = m_height - mouse_y - 1;

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));

    float depth = 0.0f;
    glsafe(::glReadPixels(mouse_x, gl_y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth));

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));

    if (out_depth != nullptr)
        *out_depth = depth;

    out_world_pos = Vec3f::Zero();

    return true;
}

ColorRGBA GPUColorPicker::id_to_color(int id)
{
    unsigned char r = (id >> 16) & 0xFF;
    unsigned char g = (id >> 8) & 0xFF;
    unsigned char b = id & 0xFF;

    return ColorRGBA(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

int GPUColorPicker::color_to_id(const ColorRGBA& color)
{
    unsigned char r = static_cast<unsigned char>(color.r() * 255.0f + 0.5f);
    unsigned char g = static_cast<unsigned char>(color.g() * 255.0f + 0.5f);
    unsigned char b = static_cast<unsigned char>(color.b() * 255.0f + 0.5f);

    return color_to_id(r, g, b);
}

int GPUColorPicker::color_to_id(unsigned char r, unsigned char g, unsigned char b)
{
    return (static_cast<int>(r) << 16) | (static_cast<int>(g) << 8) | static_cast<int>(b);
}

} // namespace GUI
} // namespace Slic3r


