/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "EllipseTool.h"
#include "ImageEditor.h"
#include "Layer.h"
#include <LibGUI/Action.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Label.h>
#include <LibGUI/Menu.h>
#include <LibGUI/Painter.h>
#include <LibGUI/RadioButton.h>
#include <LibGUI/ValueSlider.h>
#include <LibGfx/Rect.h>

namespace PixelPaint {

EllipseTool::EllipseTool()
{
}

EllipseTool::~EllipseTool()
{
}

void EllipseTool::draw_using(GUI::Painter& painter, Gfx::IntRect const& ellipse_intersecting_rect)
{
    switch (m_mode) {
    case Mode::Outline:
        painter.draw_ellipse_intersecting(ellipse_intersecting_rect, m_editor->color_for(m_drawing_button), m_thickness);
        break;
    case Mode::Fill:
        painter.fill_ellipse(ellipse_intersecting_rect, m_editor->color_for(m_drawing_button));
        break;
    default:
        VERIFY_NOT_REACHED();
    }
}

void EllipseTool::on_mousedown(Layer&, GUI::MouseEvent& event, GUI::MouseEvent&)
{
    if (event.button() != GUI::MouseButton::Left && event.button() != GUI::MouseButton::Right)
        return;

    if (m_drawing_button != GUI::MouseButton::None)
        return;

    m_drawing_button = event.button();
    m_ellipse_start_position = event.position();
    m_ellipse_end_position = event.position();
    m_editor->update();
}

void EllipseTool::on_mouseup(Layer& layer, GUI::MouseEvent& event, GUI::MouseEvent&)
{
    if (event.button() == m_drawing_button) {
        GUI::Painter painter(layer.bitmap());
        draw_using(painter, Gfx::IntRect::from_two_points(m_ellipse_start_position, m_ellipse_end_position));
        m_drawing_button = GUI::MouseButton::None;
        m_editor->update();
        m_editor->did_complete_action();
    }
}

void EllipseTool::on_mousemove(Layer&, GUI::MouseEvent& event, GUI::MouseEvent&)
{
    if (m_drawing_button == GUI::MouseButton::None)
        return;

    m_ellipse_end_position = event.position();
    m_editor->update();
}

void EllipseTool::on_second_paint(Layer const& layer, GUI::PaintEvent& event)
{
    if (m_drawing_button == GUI::MouseButton::None)
        return;

    GUI::Painter painter(*m_editor);
    painter.add_clip_rect(event.rect());
    auto preview_start = m_editor->layer_position_to_editor_position(layer, m_ellipse_start_position).to_type<int>();
    auto preview_end = m_editor->layer_position_to_editor_position(layer, m_ellipse_end_position).to_type<int>();
    draw_using(painter, Gfx::IntRect::from_two_points(preview_start, preview_end));
}

void EllipseTool::on_keydown(GUI::KeyEvent& event)
{
    if (event.key() == Key_Escape && m_drawing_button != GUI::MouseButton::None) {
        m_drawing_button = GUI::MouseButton::None;
        m_editor->update();
        event.accept();
    }
}

GUI::Widget* EllipseTool::get_properties_widget()
{
    if (!m_properties_widget) {
        m_properties_widget = GUI::Widget::construct();
        m_properties_widget->set_layout<GUI::VerticalBoxLayout>();

        auto& thickness_container = m_properties_widget->add<GUI::Widget>();
        thickness_container.set_fixed_height(20);
        thickness_container.set_layout<GUI::HorizontalBoxLayout>();

        auto& thickness_label = thickness_container.add<GUI::Label>("Thickness:");
        thickness_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        thickness_label.set_fixed_size(80, 20);

        auto& thickness_slider = thickness_container.add<GUI::ValueSlider>(Orientation::Horizontal, "px");
        thickness_slider.set_range(1, 10);
        thickness_slider.set_value(m_thickness);

        thickness_slider.on_change = [&](int value) {
            m_thickness = value;
        };

        auto& mode_container = m_properties_widget->add<GUI::Widget>();
        mode_container.set_fixed_height(46);
        mode_container.set_layout<GUI::HorizontalBoxLayout>();
        auto& mode_label = mode_container.add<GUI::Label>("Mode:");
        mode_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        mode_label.set_fixed_size(80, 20);

        auto& mode_radio_container = mode_container.add<GUI::Widget>();
        mode_radio_container.set_layout<GUI::VerticalBoxLayout>();
        auto& outline_mode_radio = mode_radio_container.add<GUI::RadioButton>("Outline");
        auto& fill_mode_radio = mode_radio_container.add<GUI::RadioButton>("Fill");

        outline_mode_radio.on_checked = [&](bool) {
            m_mode = Mode::Outline;
        };
        fill_mode_radio.on_checked = [&](bool) {
            m_mode = Mode::Fill;
        };

        outline_mode_radio.set_checked(true);
    }

    return m_properties_widget.ptr();
}

}
