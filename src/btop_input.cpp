/* Copyright 2021 Aristocratos (jakob@qvantnet.com)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4
*/

#include <iostream>
#include <ranges>

#include <btop_input.hpp>
#include <btop_tools.hpp>
#include <btop_config.hpp>
#include <btop_shared.hpp>
#include <btop_menu.hpp>
#include <btop_draw.hpp>
#include <signal.h>

using std::cin, std::string_literals::operator""s;
using namespace Tools;
namespace rng = std::ranges;

namespace Input {
	namespace {
		//* Map for translating key codes to readable values
		const unordered_flat_map<string, string> Key_escapes = {
			{"\033",	"escape"},
			{"\n",		"enter"},
			{" ",		"space"},
			{"\x7f",	"backspace"},
			{"\x08",	"backspace"},
			{"[A", 		"up"},
			{"OA",		"up"},
			{"[B", 		"down"},
			{"OB",		"down"},
			{"[D", 		"left"},
			{"OD",		"left"},
			{"[C", 		"right"},
			{"OC",		"right"},
			{"[2~",		"insert"},
			{"[3~",		"delete"},
			{"[H",		"home"},
			{"[F",		"end"},
			{"[5~",		"page_up"},
			{"[6~",		"page_down"},
			{"\t",		"tab"},
			{"[Z",		"shift_tab"},
			{"OP",		"f1"},
			{"OQ",		"f2"},
			{"OR",		"f3"},
			{"OS",		"f4"},
			{"[15~",	"f5"},
			{"[17~",	"f6"},
			{"[18~",	"f7"},
			{"[19~",	"f8"},
			{"[20~",	"f9"},
			{"[21~",	"f10"},
			{"[23~",	"f11"},
			{"[24~",	"f12"}
		};
	}

	std::atomic<bool> interrupt (false);
	array<int, 2> mouse_pos;
	unordered_flat_map<string, Mouse_loc> mouse_mappings;

	deque<string> history(50, "");
	string old_filter;

	bool poll(int timeout) {
		if (timeout < 1) return cin.rdbuf()->in_avail() > 0;
		while (timeout > 0) {
			if (interrupt) return interrupt = false;
			if (cin.rdbuf()->in_avail() > 0) return true;
			sleep_ms(timeout < 10 ? timeout : 10);
			timeout -= 10;
		}
		return false;
	}

	string get() {
		string key;
		while (cin.rdbuf()->in_avail() > 0 and key.size() < 100) key += cin.get();
		if (cin.rdbuf()->in_avail() > 0) cin.ignore(SSmax);
		if (not key.empty()) {
			//? Remove escape code prefix if present
			if (key.substr(0, 2) == Fx::e) {
				key.erase(0, 1);
			}
			//? Detect if input is an mouse event
			if (key.starts_with("[<")) {
				std::string_view key_view = key;
				string mouse_event;
				if (key_view.starts_with("[<0;") and key_view.ends_with('M')) {
					mouse_event = "mouse_click";
					key_view.remove_prefix(4);
				}
				else if (key_view.starts_with("[<0;") and key_view.ends_with('m')) {
					mouse_event = "mouse_release";
					key_view.remove_prefix(4);
				}
				else if (key_view.starts_with("[<64;")) {
					mouse_event = "mouse_scroll_up";
					key_view.remove_prefix(5);
				}
				else if (key_view.starts_with("[<65;")) {
					mouse_event = "mouse_scroll_down";
					key_view.remove_prefix(5);
				}
				else
					key.clear();

				if (Config::getB("proc_filtering")) {
					if (mouse_event == "mouse_click") return mouse_event;
					else return "";
				}

				//? Get column and line position of mouse and check for any actions mapped to current position
				if (not key.empty()) {
					try {
						const auto delim = key_view.find(';');
						mouse_pos[0] = stoi((string)key_view.substr(0, delim));
						mouse_pos[1] = stoi((string)key_view.substr(delim + 1, key_view.find('M', delim)));
					}
					catch (const std::invalid_argument&) { mouse_event.clear(); }
					catch (const std::out_of_range&) { mouse_event.clear(); }

					key = mouse_event;

					if (not Menu::active and key == "mouse_click") {
						const auto& [col, line] = mouse_pos;

						for (const auto& [mapped_key, pos] : mouse_mappings) {
							if (col >= pos.col and col < pos.col + pos.width and line >= pos.line and line < pos.line + pos.height) {
								key = mapped_key;
								break;
							}
						}
					}
				}

			}
			else if (Key_escapes.contains(key))
				key = Key_escapes.at(key);
			else if (ulen(key) > 1)
				key.clear();

			history.push_back(key);
			history.pop_front();
		}
		return key;
	}

	string wait() {
		while (cin.rdbuf()->in_avail() < 1) {
			sleep_ms(10);
		}
		return get();
	}

	void clear() {
		if (cin.rdbuf()->in_avail() > 0) cin.ignore(SSmax);
		history.clear();
	}

	void process(const string& key) {
		if (key.empty()) return;
		try {
			auto& filtering = Config::getB("proc_filtering");
			if (not filtering and key == "q") exit(0);

			//? Global input actions
			if (not filtering) {
				bool keep_going = false;
				if (is_in(key, "1", "2", "3", "4")) {
					static const array<string, 4> boxes = {"cpu", "mem", "net", "proc"};
					Config::toggle_box(boxes.at(std::stoi(key) - 1));
					term_resize(true);
				}
				else
					keep_going = true;

				if (not keep_going) return;
			}

			//? Input actions for proc box
			if (Proc::shown) {
				bool keep_going = false;
				bool no_update = true;
				bool redraw = true;
				if (filtering) {
					if (key == "enter") {
						Config::set("proc_filter", Proc::filter.text);
						Config::set("proc_filtering", false);
						old_filter.clear();
					}
					else if (key == "escape" or key == "mouse_click") {
						Config::set("proc_filter", old_filter);
						Config::set("proc_filtering", false);
						old_filter.clear();
					}
					else if (Proc::filter.command(key)) {
						if (Config::getS("proc_filter") != Proc::filter.text)
							Config::set("proc_filter", Proc::filter.text);
					}
					else
						return;
				}
				else if (key == "left") {
					int cur_i = v_index(Proc::sort_vector, Config::getS("proc_sorting"));
					if (--cur_i < 0)
						cur_i = Proc::sort_vector.size() - 1;
					Config::set("proc_sorting", Proc::sort_vector.at(cur_i));
				}
				else if (key == "right") {
					int cur_i = v_index(Proc::sort_vector, Config::getS("proc_sorting"));
					if (std::cmp_greater(++cur_i, Proc::sort_vector.size() - 1))
						cur_i = 0;
					Config::set("proc_sorting", Proc::sort_vector.at(cur_i));
				}
				else if (key == "f") {
					Config::flip("proc_filtering");
					Proc::filter = { Config::getS("proc_filter") };
					old_filter = Proc::filter.text;
				}
				else if (key == "e")
					Config::flip("proc_tree");

				else if (key == "r")
					Config::flip("proc_reversed");

				else if (key == "c")
					Config::flip("proc_per_core");

				else if (key == "delete" and not Config::getS("proc_filter").empty())
					Config::set("proc_filter", ""s);

				else if (key == "ö") {
					if (Global::overlay.empty())
						Global::overlay = Mv::to(Term::height / 2, Term::width / 2) + "\x1b[1;32mTESTING";
					else
						Global::overlay.clear();
					Runner::run("all", true, true);
				}
				else if (key.starts_with("mouse_")) {
					redraw = false;
					const auto& [col, line] = mouse_pos;
					const int y = (Config::getB("show_detailed") ? Proc::y + 8 : Proc::y);
					const int height = (Config::getB("show_detailed") ? Proc::height - 8 : Proc::height);
					if (col >= Proc::x + 1 and col < Proc::x + Proc::width and line >= y + 1 and line < y + height - 1) {
						if (key == "mouse_click") {
							if (col < Proc::x + Proc::width - 2) {
								const auto& current_selection = Config::getI("proc_selected");
								if (current_selection == line - y - 1) {
									redraw = true;
									goto proc_mouse_enter;
								}
								else if (current_selection == 0 or line - y - 1 == 0)
									redraw = true;
								Config::set("proc_selected", line - y - 1);
							}
							else if (line == y + 1) {
								if (Proc::selection("page_up") == -1) return;
							}
							else if (line == y + height - 2) {
								if (Proc::selection("page_down") == -1) return;
							}
							else if (Proc::selection("mousey" + to_string(line - y - 2)) == -1)
								return;
						}
						else
							goto proc_mouse_scroll;
					}
					else if (key == "mouse_click" and Config::getI("proc_selected") > 0) {
						Config::set("proc_selected", 0);
						redraw = true;
					}
					else
						keep_going = true;
				}
				else if (key == "enter") {
					proc_mouse_enter:
					if (Config::getI("proc_selected") == 0 and not Config::getB("show_detailed")) {
						return;
					}
					else if (Config::getI("proc_selected") > 0 and Config::getI("detailed_pid") != Config::getI("selected_pid")) {
						Config::set("detailed_pid", Config::getI("selected_pid"));
						Config::set("proc_last_selected", Config::getI("proc_selected"));
						Config::set("proc_selected", 0);
						Config::set("show_detailed", true);
					}
					else if (Config::getB("show_detailed")) {
						if (Config::getI("proc_last_selected") > 0) Config::set("proc_selected", Config::getI("proc_last_selected"));
						Config::set("proc_last_selected", 0);
						Config::set("detailed_pid", 0);
						Config::set("show_detailed", false);
					}
				}
				else if (is_in(key, "+", "-", "space") and Config::getB("proc_tree") and Config::getI("proc_selected") > 0) {
					atomic_wait(Runner::active);
					auto& pid = Config::getI("selected_pid");
					if (key == "+" or key == "space") Proc::expand = pid;
					if (key == "-" or key == "space") Proc::collapse = pid;
				}
				else if (key == "t") {
					Logger::debug(key);
					return;
				}
				else if (key == "k") {
					Logger::debug(key);
					return;
				}
				else if (key == "s") {
					Logger::debug(key);
					return;
				}
				else if (is_in(key, "up", "down", "page_up", "page_down", "home", "end")) {
					proc_mouse_scroll:
					redraw = false;
					auto old_selected = Config::getI("proc_selected");
					auto new_selected = Proc::selection(key);
					if (new_selected == -1)
						return;
					else if (old_selected != new_selected and (old_selected == 0 or new_selected == 0))
						redraw = true;
				}
				else keep_going = true;

				if (not keep_going) {
					Runner::run("proc", no_update, redraw);
					return;
				}
			}

			//? Input actions for cpu box
			if (Cpu::shown) {
				bool keep_going = false;
				bool no_update = true;
				bool redraw = true;
				static uint64_t last_press = 0;

				if (key == "+" and Config::getI("update_ms") <= 86399900) {
					int add = (Config::getI("update_ms") <= 86399000 and last_press >= time_ms() - 200
						and rng::all_of(Input::history, [](const auto& str){ return str == "+"; })
						? 1000 : 100);
					Config::set("update_ms", Config::getI("update_ms") + add);
					last_press = time_ms();
					redraw = true;
				}
				else if (key == "-" and Config::getI("update_ms") >= 200) {
					int sub = (Config::getI("update_ms") >= 2000 and last_press >= time_ms() - 200
						and rng::all_of(Input::history, [](const auto& str){ return str == "-"; })
						? 1000 : 100);
					Config::set("update_ms", Config::getI("update_ms") - sub);
					last_press = time_ms();
					redraw = true;
				}
				else keep_going = true;

				if (not keep_going) {
					Runner::run("cpu", no_update, redraw);
					return;
				}
			}

			//? Input actions for mem box
			if (Mem::shown) {
				bool keep_going = false;
				bool no_update = true;
				bool redraw = true;

				if (key == "i") {
					Config::flip("io_mode");
				}

				if (not keep_going) {
					Runner::run("mem", no_update, redraw);
					return;
				}
			}
		}


		catch (const std::exception& e) {
			throw std::runtime_error("Input::process(\"" + key + "\") : " + (string)e.what());
		}
	}

}