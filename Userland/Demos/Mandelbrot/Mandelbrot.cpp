/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Gunnar Beutner <gbeutner@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGUI/Action.h>
#include <LibGUI/Application.h>
#include <LibGUI/FilePicker.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Menu.h>
#include <LibGUI/Menubar.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/Painter.h>
#include <LibGUI/Widget.h>
#include <LibGUI/Window.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/PNGWriter.h>
#include <unistd.h>

class MandelbrotSet {
public:
    MandelbrotSet()
    {
        set_view();
    }

    void reset()
    {
        set_view();
        correct_aspect();
        calculate();
    }

    void resize(Gfx::IntSize const& size)
    {
        m_bitmap = Gfx::Bitmap::try_create(Gfx::BitmapFormat::BGRx8888, size);
        correct_aspect();
        calculate();
    }

    void zoom(Gfx::IntRect const& rect)
    {
        set_view(
            rect.left() * (m_x_end - m_x_start) / m_bitmap->width() + m_x_start,
            rect.right() * (m_x_end - m_x_start) / m_bitmap->width() + m_x_start,
            rect.top() * (m_y_end - m_y_start) / m_bitmap->height() + m_y_start,
            rect.bottom() * (m_y_end - m_y_start) / m_bitmap->height() + m_y_start);
        correct_aspect();
        calculate();
    }

    void pan_by(Gfx::IntPoint const& delta)
    {
        auto relative_width_pixel = (m_x_end - m_x_start) / m_bitmap->width();
        auto relative_height_pixel = (m_y_end - m_y_start) / m_bitmap->height();

        set_view(
            m_x_start - delta.x() * relative_width_pixel,
            m_x_end - delta.x() * relative_width_pixel,
            m_y_start - delta.y() * relative_height_pixel,
            m_y_end - delta.y() * relative_height_pixel);

        Gfx::IntRect horizontal_missing, vertical_missing;
        if (delta.y() >= 0) {
            horizontal_missing = { 0, 0, m_bitmap->width(), delta.y() };
        } else {
            horizontal_missing = { 0, m_bitmap->height() + delta.y(), m_bitmap->width(), -delta.y() };
        }

        if (delta.x() >= 0) {
            vertical_missing = { 0, 0, delta.x(), m_bitmap->height() };
        } else {
            vertical_missing = { m_bitmap->width() + delta.x(), 0, -delta.x(), m_bitmap->height() };
        }

        move_contents_by(delta);
        calculate_rect(horizontal_missing);
        calculate_rect(vertical_missing);
    }

    double mandelbrot(double px, double py, i32 max_iterations)
    {
        // Based on https://en.wikipedia.org/wiki/Plotting_algorithms_for_the_Mandelbrot_set
        const double x0 = px * (m_x_end - m_x_start) / m_bitmap->width() + m_x_start;
        const double y0 = py * (m_y_end - m_y_start) / m_bitmap->height() + m_y_start;
        double x = 0;
        double y = 0;
        i32 iteration = 0;
        double x2 = 0;
        double y2 = 0;

        while (x2 + y2 <= 4 && iteration < max_iterations) {
            y = 2 * x * y + y0;
            x = x2 - y2 + x0;
            x2 = x * x;
            y2 = y * y;
            iteration++;
        }

        if (iteration == max_iterations)
            return iteration;

        auto lz = sqrt(x * x + y * y) / 2;
        return 1 + iteration + log(lz / log(2)) / log(2);
    }

    static double linear_interpolate(double v0, double v1, double t)
    {
        return v0 + t * (v1 - v0);
    }

    void calculate_pixel(int px, int py, int max_iterations)
    {
        auto iterations = mandelbrot(px, py, max_iterations);
        auto whole_iterations = floor(iterations);
        auto partial_iterations = fmod(iterations, 1);
        double hue1 = whole_iterations * 360.0 / max_iterations;
        if (hue1 >= 360.0)
            hue1 = 0.0;
        double hue2 = (whole_iterations + 1) * 360.0 / max_iterations;
        if (hue2 >= 360.0)
            hue2 = 0.0;
        double hue = linear_interpolate(hue1, hue2, partial_iterations);
        double saturation = 1.0;
        double value = iterations < max_iterations ? 1.0 : 0;
        m_bitmap->set_pixel(px, py, Color::from_hsv(hue, saturation, value));
    }

    void calculate(int max_iterations = 100)
    {
        VERIFY(m_bitmap);

        calculate_rect(m_bitmap->rect(), max_iterations);
    }

    void calculate_rect(Gfx::IntRect const& rect, int max_iterations = 100)
    {
        VERIFY(m_bitmap);

        if (rect.is_empty())
            return;

        for (int py = rect.top(); py <= rect.bottom(); py++)
            for (int px = rect.left(); px <= rect.right(); px++)
                calculate_pixel(px, py, max_iterations);
    }

    Gfx::Bitmap const& bitmap() const
    {
        return *m_bitmap;
    }

private:
    double m_x_start { 0 };
    double m_x_end { 0 };
    double m_y_start { 0 };
    double m_y_end { 0 };
    RefPtr<Gfx::Bitmap> m_bitmap;

    void set_view(double x_start = -2.5, double x_end = 1.0, double y_start = -1.75, double y_end = 1.75)
    {
        m_x_start = x_start;
        m_x_end = x_end;
        m_y_start = y_start;
        m_y_end = y_end;
    }

    void correct_aspect()
    {
        auto y_mid = m_y_start + (m_y_end - m_y_start) / 2;
        auto aspect_corrected_y_length = (m_x_end - m_x_start) * m_bitmap->height() / m_bitmap->width();
        m_y_start = y_mid - aspect_corrected_y_length / 2;
        m_y_end = y_mid + aspect_corrected_y_length / 2;
    }

    void move_contents_by(Gfx::IntPoint const& delta)
    {
        // If we're moving down we paint upwards, else we paint downwards, to
        // avoid overwriting.
        if (delta.y() >= 0) {
            for (int row = m_bitmap->physical_height() - 1; row >= delta.y(); row--)
                move_row(row - delta.y(), row, delta.x());
        } else {
            for (int row = 0; row < m_bitmap->physical_height() + delta.y(); row++)
                move_row(row - delta.y(), row, delta.x());
        }
    }

    void move_row(int from, int to, int x_delta)
    {
        // If we're moving right we paint RTL, else we paint LTR, to avoid
        // overwriting.
        if (x_delta >= 0) {
            for (int column = m_bitmap->physical_width() - 1; column >= x_delta; column--) {
                m_bitmap->set_pixel(column, to, m_bitmap->get_pixel(column - x_delta, from));
            }
        } else {
            for (int column = 0; column < m_bitmap->physical_width() + x_delta; column++) {
                m_bitmap->set_pixel(column, to, m_bitmap->get_pixel(column - x_delta, from));
            }
        }
    }
};

class Mandelbrot : public GUI::Frame {
    C_OBJECT(Mandelbrot)

    void export_image(String const& export_path);

private:
    virtual void paint_event(GUI::PaintEvent&) override;
    virtual void mousedown_event(GUI::MouseEvent& event) override;
    virtual void mousemove_event(GUI::MouseEvent& event) override;
    virtual void mouseup_event(GUI::MouseEvent& event) override;
    virtual void mousewheel_event(GUI::MouseEvent& event) override;
    virtual void resize_event(GUI::ResizeEvent& event) override;

    bool m_dragging { false };
    Gfx::IntPoint m_selection_start;
    Gfx::IntPoint m_selection_end;

    bool m_panning { false };
    Gfx::IntPoint m_last_pan_position;

    MandelbrotSet m_set;
};

void Mandelbrot::paint_event(GUI::PaintEvent& event)
{
    Frame::paint_event(event);

    GUI::Painter painter(*this);
    painter.add_clip_rect(frame_inner_rect());
    painter.add_clip_rect(event.rect());
    painter.draw_scaled_bitmap(rect(), m_set.bitmap(), m_set.bitmap().rect());

    if (m_dragging)
        painter.draw_rect(Gfx::IntRect::from_two_points(m_selection_start, m_selection_end), Color::Blue);
}

void Mandelbrot::mousedown_event(GUI::MouseEvent& event)
{
    if (event.button() == GUI::MouseButton::Left) {
        if (!m_dragging) {
            m_selection_start = event.position();
            m_selection_end = event.position();
            m_dragging = true;
            update();
        }
    } else if (event.button() == GUI::MouseButton::Middle) {
        if (!m_panning) {
            m_last_pan_position = event.position();
            m_panning = true;
            update();
        }
    }

    return GUI::Widget::mousedown_event(event);
}

void Mandelbrot::mousemove_event(GUI::MouseEvent& event)
{
    if (m_dragging) {
        // Maintain aspect ratio
        int selection_width = event.position().x() - m_selection_start.x();
        int selection_height = event.position().y() - m_selection_start.y();
        int aspect_corrected_selection_width = selection_height * width() / height();
        int aspect_corrected_selection_height = selection_width * height() / width();
        if (selection_width * aspect_corrected_selection_height > aspect_corrected_selection_width * selection_height)
            m_selection_end = { event.position().x(), m_selection_start.y() + abs(aspect_corrected_selection_height) * ((selection_height < 0) ? -1 : 1) };
        else
            m_selection_end = { m_selection_start.x() + abs(aspect_corrected_selection_width) * ((selection_width < 0) ? -1 : 1), event.position().y() };
        update();
    }

    if (m_panning) {
        m_set.pan_by(event.position() - m_last_pan_position);
        m_last_pan_position = event.position();

        update();
    }

    return GUI::Widget::mousemove_event(event);
}

void Mandelbrot::mouseup_event(GUI::MouseEvent& event)
{
    if (event.button() == GUI::MouseButton::Left) {
        auto selection = Gfx::IntRect::from_two_points(m_selection_start, m_selection_end);
        if (selection.width() > 0 && selection.height() > 0)
            m_set.zoom(selection);
        m_dragging = false;
        update();
    } else if (event.button() == GUI::MouseButton::Middle) {
        m_panning = false;
        update();
    } else if (event.button() == GUI::MouseButton::Right) {
        m_set.reset();
        update();
    }

    return GUI::Widget::mouseup_event(event);
}

void Mandelbrot::mousewheel_event(GUI::MouseEvent& event)
{
    static constexpr double zoom_in_multiplier = 0.8;
    static constexpr double zoom_out_multiplier = 1.25;

    bool zooming_in = event.wheel_delta() < 0;
    double multiplier = zooming_in ? zoom_in_multiplier : zoom_out_multiplier;

    auto relative_rect = this->relative_rect();
    Gfx::IntRect zoomed_rect = relative_rect;

    zoomed_rect.set_width(zoomed_rect.width() * multiplier);
    zoomed_rect.set_height(zoomed_rect.height() * multiplier);

    auto leftover_width = abs(relative_rect.width() - zoomed_rect.width());
    auto leftover_height = abs(relative_rect.height() - zoomed_rect.height());

    double cursor_x_percentage = static_cast<double>(event.position().x()) / relative_rect.width();
    double cursor_y_percentage = static_cast<double>(event.position().y()) / relative_rect.height();

    zoomed_rect.set_x((zooming_in ? 1 : -1) * leftover_width * cursor_x_percentage);
    zoomed_rect.set_y((zooming_in ? 1 : -1) * leftover_height * cursor_y_percentage);

    m_set.zoom(zoomed_rect);
    update();
}

void Mandelbrot::resize_event(GUI::ResizeEvent& event)
{
    m_set.resize(event.size());
}

void Mandelbrot::export_image(String const& export_path)
{
    m_set.resize(Gfx::IntSize { 1920, 1080 });
    auto png = Gfx::PNGWriter::encode(m_set.bitmap());
    m_set.resize(size());
    auto file = fopen(export_path.characters(), "wb");
    if (!file) {
        GUI::MessageBox::show(window(), String::formatted("Could not open '{}' for writing.", export_path), "Mandelbrot", GUI::MessageBox::Type::Error);
        return;
    }
    fwrite(png.data(), 1, png.size(), file);
    fclose(file);
    GUI::MessageBox::show(window(), "Image was successfully exported.", "Mandelbrot", GUI::MessageBox::Type::Information);
}

int main(int argc, char** argv)
{
    auto app = GUI::Application::construct(argc, argv);

    if (pledge("stdio thread recvfd sendfd rpath wpath cpath", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

#if 0
    if (unveil("/res", "r") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil(nullptr, nullptr) < 0) {
        perror("unveil");
        return 1;
    }
#endif

    auto window = GUI::Window::construct();
    window->set_double_buffering_enabled(false);
    window->set_title("Mandelbrot");
    window->set_minimum_size(320, 240);
    window->resize(window->minimum_size() * 2);
    auto& mandelbrot = window->set_main_widget<Mandelbrot>();

    auto& file_menu = window->add_menu("&File");
    file_menu.add_action(GUI::Action::create("&Export...", { Mod_Ctrl | Mod_Shift, Key_S }, Gfx::Bitmap::try_load_from_file("/res/icons/16x16/save.png"),
        [&](GUI::Action&) {
            Optional<String> export_path = GUI::FilePicker::get_save_filepath(window, "untitled", "png");
            if (!export_path.has_value())
                return;
            mandelbrot.export_image(export_path.value());
        }));
    file_menu.add_separator();
    file_menu.add_action(GUI::CommonActions::make_quit_action([&](auto&) { app->quit(); }));
    window->show();

    auto app_icon = GUI::Icon::default_icon("app-mandelbrot");
    window->set_icon(app_icon.bitmap_for_size(16));

    return app->exec();
}
