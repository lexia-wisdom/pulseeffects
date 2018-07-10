#include <glibmm.h>
#include <glibmm/i18n.h>
#include "convolver_ui.hpp"

namespace {

gboolean buffersize_enum_to_int(GValue* value,
                                GVariant* variant,
                                gpointer user_data) {
    auto v = g_variant_get_string(variant, nullptr);

    if (v == std::string("64")) {
        g_value_set_int(value, 0);
    } else if (v == std::string("128")) {
        g_value_set_int(value, 1);
    } else if (v == std::string("256")) {
        g_value_set_int(value, 2);
    } else if (v == std::string("512")) {
        g_value_set_int(value, 3);
    } else if (v == std::string("1024")) {
        g_value_set_int(value, 4);
    } else if (v == std::string("2048")) {
        g_value_set_int(value, 5);
    } else if (v == std::string("4096")) {
        g_value_set_int(value, 6);
    } else if (v == std::string("8192")) {
        g_value_set_int(value, 7);
    }

    return true;
}

GVariant* int_to_buffersize_enum(const GValue* value,
                                 const GVariantType* expected_type,
                                 gpointer user_data) {
    int v = g_value_get_int(value);

    if (v == 0) {
        return g_variant_new_string("64");
    } else if (v == 1) {
        return g_variant_new_string("128");
    } else if (v == 2) {
        return g_variant_new_string("256");
    } else if (v == 3) {
        return g_variant_new_string("512");
    } else if (v == 4) {
        return g_variant_new_string("1024");
    } else if (v == 5) {
        return g_variant_new_string("2048");
    } else if (v == 6) {
        return g_variant_new_string("4096");
    } else {
        return g_variant_new_string("8192");
    }
}

}  // namespace

ConvolverUi::ConvolverUi(BaseObjectType* cobject,
                         const Glib::RefPtr<Gtk::Builder>& builder,
                         const std::string& settings_name)
    : Gtk::Grid(cobject),
      PluginUiBase(builder, settings_name),
      irs_dir(Glib::get_user_config_dir() + "/PulseEffects/irs") {
    name = "convolver";

    // loading glade widgets

    builder->get_widget("irs_listbox", irs_listbox);
    builder->get_widget("irs_menu_button", irs_menu_button);
    builder->get_widget("irs_scrolled_window", irs_scrolled_window);
    builder->get_widget("import_irs", import_irs);
    builder->get_widget("buffersize", buffersize);
    builder->get_widget("left_plot", left_plot);
    builder->get_widget("right_plot", right_plot);

    get_object(builder, "input_gain", input_gain);
    get_object(builder, "output_gain", output_gain);
    get_object(builder, "ir_width", ir_width);

    irs_menu_button->signal_clicked().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_irs_menu_button_clicked));

    irs_listbox->set_sort_func(
        sigc::mem_fun(*this, &ConvolverUi::on_listbox_sort));

    irs_listbox->signal_row_activated().connect([&](auto row) {
        auto irs_file =
            irs_dir / boost::filesystem::path{row->get_name() + ".irs"};

        settings->set_string("kernel-path", irs_file.string());
    });

    import_irs->signal_clicked().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_import_irs_clicked));

    // drawing area callbacks

    left_plot->signal_draw().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_left_draw));
    left_plot->signal_motion_notify_event().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_left_motion_notify_event));
    left_plot->signal_enter_notify_event().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_mouse_enter_notify_event));
    left_plot->signal_leave_notify_event().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_mouse_leave_notify_event));

    right_plot->signal_draw().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_right_draw));
    right_plot->signal_motion_notify_event().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_right_motion_notify_event));
    right_plot->signal_enter_notify_event().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_mouse_enter_notify_event));
    right_plot->signal_leave_notify_event().connect(
        sigc::mem_fun(*this, &ConvolverUi::on_mouse_leave_notify_event));

    // gsettings bindings

    auto flag = Gio::SettingsBindFlags::SETTINGS_BIND_DEFAULT;

    settings->bind("installed", this, "sensitive", flag);
    settings->bind("input-gain", input_gain.get(), "value", flag);
    settings->bind("output-gain", output_gain.get(), "value", flag);
    settings->bind("ir-width", ir_width.get(), "value", flag);

    g_settings_bind_with_mapping(
        settings->gobj(), "buffersize", buffersize->gobj(), "active",
        G_SETTINGS_BIND_DEFAULT, buffersize_enum_to_int, int_to_buffersize_enum,
        nullptr, nullptr);

    settings->set_boolean("post-messages", true);

    // irs dir

    auto dir_exists = boost::filesystem::is_directory(irs_dir);

    if (!dir_exists) {
        if (boost::filesystem::create_directories(irs_dir)) {
            util::debug(log_tag + "irs directory created: " + irs_dir.string());
        } else {
            util::warning(log_tag + "failed to create irs directory: " +
                          irs_dir.string());
        }

    } else {
        util::debug(log_tag +
                    "irs directory already exists: " + irs_dir.string());
    }
}

ConvolverUi::~ConvolverUi() {
    settings->set_boolean("post-messages", false);

    for (auto c : connections) {
        c.disconnect();
    }

    util::debug(name + " ui destroyed");
}

std::vector<std::string> ConvolverUi::get_irs_names() {
    boost::filesystem::directory_iterator it{irs_dir};
    std::vector<std::string> names;

    while (it != boost::filesystem::directory_iterator{}) {
        if (boost::filesystem::is_regular_file(it->status())) {
            if (it->path().extension().string() == ".irs") {
                names.push_back(it->path().stem().string());
            }
        }

        it++;
    }

    return names;
}

void ConvolverUi::import_irs_file(const std::string& file_path) {
    boost::filesystem::path p{file_path};

    if (boost::filesystem::is_regular_file(p)) {
        if (p.extension().string() == ".irs") {
            auto out_path = irs_dir / p.filename();

            boost::filesystem::copy_file(
                p, out_path,
                boost::filesystem::copy_option::overwrite_if_exists);

            util::debug(log_tag + "imported irs file to: " + out_path.string());
        }
    } else {
        util::warning(log_tag + p.string() + " is not a file!");
    }
}

void ConvolverUi::remove_irs_file(const std::string& name) {
    auto irs_file = irs_dir / boost::filesystem::path{name + ".irs"};

    if (boost::filesystem::exists(irs_file)) {
        boost::filesystem::remove(irs_file);

        util::debug(log_tag + "removed irs file: " + irs_file.string());
    }
}

int ConvolverUi::on_listbox_sort(Gtk::ListBoxRow* row1, Gtk::ListBoxRow* row2) {
    auto name1 = row1->get_name();
    auto name2 = row2->get_name();

    std::vector<std::string> names = {name1, name2};

    std::sort(names.begin(), names.end());

    if (name1 == names[0]) {
        return -1;
    } else if (name2 == names[0]) {
        return 1;
    } else {
        return 0;
    }
}

void ConvolverUi::populate_irs_listbox() {
    auto children = irs_listbox->get_children();

    for (auto c : children) {
        irs_listbox->remove(*c);
    }

    auto names = get_irs_names();

    for (auto name : names) {
        auto b = Gtk::Builder::create_from_resource(
            "/com/github/wwmm/pulseeffects/ui/irs_row.glade");

        Gtk::ListBoxRow* row;
        Gtk::Button* remove_btn;
        Gtk::Label* label;

        b->get_widget("irs_row", row);
        b->get_widget("remove", remove_btn);
        b->get_widget("name", label);

        row->set_name(name);
        label->set_text(name);

        connections.push_back(remove_btn->signal_clicked().connect([=]() {
            remove_irs_file(name);
            populate_irs_listbox();
        }));

        irs_listbox->add(*row);
        irs_listbox->show_all();
    }
}

void ConvolverUi::on_irs_menu_button_clicked() {
    int height = 0.7 * this->get_toplevel()->get_allocated_height();

    irs_scrolled_window->set_max_content_height(height);

    populate_irs_listbox();
}

void ConvolverUi::on_import_irs_clicked() {
    // gtkmm 3.22 does not have FileChooseNative so we have to use C api :-(

    gint res;

    auto dialog = gtk_file_chooser_native_new(
        _("Import Impulse Response"), (GtkWindow*)this->get_toplevel()->gobj(),
        GTK_FILE_CHOOSER_ACTION_OPEN, _("Open"), _("Cancel"));

    auto filter = gtk_file_filter_new();

    gtk_file_filter_set_name(filter, _("Impulse Response"));
    gtk_file_filter_add_pattern(filter, "*.irs");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog));

    if (res == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

        auto file_list = gtk_file_chooser_get_filenames(chooser);

        g_slist_foreach(file_list,
                        [](auto data, auto user_data) {
                            auto cui = static_cast<ConvolverUi*>(user_data);

                            auto file_path = static_cast<char*>(data);

                            cui->import_irs_file(file_path);
                        },
                        this);

        g_slist_free(file_list);
    }

    g_object_unref(dialog);

    populate_irs_listbox();
}

void ConvolverUi::draw_channel(Gtk::DrawingArea* da,
                               const Cairo::RefPtr<Cairo::Context>& ctx,
                               const std::vector<float>& magnitudes) {
    auto n_bars = magnitudes.size();

    if (n_bars > 0) {
        auto allocation = da->get_allocation();
        auto width = allocation.get_width();
        auto height = allocation.get_height();
        auto n_bars = magnitudes.size();
        auto x = util::linspace(0, width, n_bars);

        for (uint n = 0; n < n_bars; n++) {
            auto bar_height = magnitudes[n] * height;

            ctx->rectangle(x[n], height - bar_height, width / n_bars,
                           bar_height);
        }

        auto color = Gdk::RGBA();
        auto style_ctx = da->get_style_context();

        style_ctx->lookup_color("theme_selected_bg_color", color);

        ctx->set_source_rgba(color.get_red(), color.get_green(),
                             color.get_blue(), 1.0);

        ctx->set_line_width(1.1);
        ctx->stroke();

        if (mouse_inside) {
            std::ostringstream msg;

            msg.precision(0);
            msg << std::fixed << mouse_freq << " Hz, ";
            msg << std::fixed << mouse_intensity << " dB";

            Pango::FontDescription font;
            font.set_family("Monospace");
            font.set_weight(Pango::WEIGHT_BOLD);

            int text_width;
            int text_height;
            auto layout = create_pango_layout(msg.str());
            layout->set_font_description(font);
            layout->get_pixel_size(text_width, text_height);

            ctx->move_to(width - text_width, 0);

            layout->show_in_cairo_context(ctx);
        }
    }
}

void ConvolverUi::update_mouse_info(GdkEventMotion* event,
                                    const int& height,
                                    const int& width) {
    mouse_freq = pow(10, 1.3 + event->x * 3.0 / width);

    // intensity scale is in decibel
    // minimum intensity is -120 dB and maximum is 0 dB

    mouse_intensity = -event->y * 120 / height;
}

bool ConvolverUi::on_left_draw(const Cairo::RefPtr<Cairo::Context>& ctx) {
    ctx->paint();

    draw_channel(left_plot, ctx, left_mag);

    return false;
}

bool ConvolverUi::on_left_motion_notify_event(GdkEventMotion* event) {
    auto allocation = left_plot->get_allocation();

    update_mouse_info(event, allocation.get_width(), allocation.get_height());

    left_plot->queue_draw();

    return false;
}

bool ConvolverUi::on_right_draw(const Cairo::RefPtr<Cairo::Context>& ctx) {
    ctx->paint();

    draw_channel(right_plot, ctx, right_mag);

    return false;
}

bool ConvolverUi::on_right_motion_notify_event(GdkEventMotion* event) {
    auto allocation = right_plot->get_allocation();

    update_mouse_info(event, allocation.get_width(), allocation.get_height());

    right_plot->queue_draw();

    return false;
}

bool ConvolverUi::on_mouse_enter_notify_event(GdkEventCrossing* event) {
    mouse_inside = true;
    return false;
}

bool ConvolverUi::on_mouse_leave_notify_event(GdkEventCrossing* event) {
    mouse_inside = false;
    return false;
}

void ConvolverUi::reset() {
    settings->reset("state");
    settings->reset("input-gain");
    settings->reset("output-gain");
    settings->reset("kernel-path");
    settings->reset("buffersize");
    settings->reset("ir-width");
}