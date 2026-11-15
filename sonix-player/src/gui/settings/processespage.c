#include "processespage.h"

#include "lvgl/lvgl.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/lang.h"
#include "src/system/core/utils.h"
#include "src/system/device/power.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

lv_obj_t *processespage_screen;

// The page container (a scrolling column) and the parts rebuilt on every open:
// the RAM card at the top and the process list below it.
static lv_obj_t *page_container;
static lv_obj_t *ram_label;
static lv_obj_t *cpu_label;
static lv_timer_t *cpu_timer;
static bool cpu_baseline;
static unsigned long long cpu_last_total, cpu_last_idle;
static char cpu_thermal_path[160];
static lv_obj_t *proc_list; // emptied and refilled on every SCREEN_LOADED

// A process, reduced to what is worth showing.
typedef struct {
	long rss_kb;
	char name[24];
} proc_row_t;

#define PROC_MAX 200	 // how many are collected
#define PROC_SHOWN 60	 // how many are shown (the largest)

// One line of /proc/meminfo, in kB. -1 when absent.
static long meminfo_kb(const char *key) {
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f) {
		return -1;
	}
	char line[160];
	size_t klen = strlen(key);
	long val = -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
			val = atol(line + klen + 1);
			break;
		}
	}
	fclose(f);
	return val;
}

// Read only. No governor, clock, voltage, priority or thermal writes.
// Missing kernel interfaces are normal and must never be treated as zero.
static bool read_text(const char *path, char *out, size_t size) {
	FILE *f = fopen(path, "r");
	if (!f) return false;
	bool ok = fgets(out, (int)size, f) != NULL;
	fclose(f);
	if (ok) {
		out[strcspn(out, "\r\n")] = 0;
		ok = out[0] != 0;
	}
	if (!ok) out[0] = 0;
	return ok;
}

static bool frequency_text(const char *name, char *out, size_t size) {
	char path[160];
	const char *roots[] = {"/sys/devices/system/cpu/cpufreq/policy0",
						   "/sys/devices/system/cpu/cpu0/cpufreq"};
	for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
		snprintf(path, sizeof(path), "%s/%s", roots[i], name);
		if (read_text(path, out, size)) return true;
	}
	return false;
}

static long positive_number(const char *text) {
	char *end;
	long n = strtol(text, &end, 10);
	return end != text && !*end && n > 0 ? n : -1;
}

static bool cpu_totals(unsigned long long *total, unsigned long long *idle) {
	char line[256];
	if (!read_text("/proc/stat", line, sizeof(line)) || strncmp(line, "cpu ", 4)) return false;
	unsigned long long v[8] = {0};
	int n = sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
				   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
	if (n < 4) return false;
	*total = 0;
	for (int i = 0; i < n; ++i) *total += v[i];
	*idle = v[3] + v[4];
	return true;
}

static void find_cpu_thermal(void) {
	cpu_thermal_path[0] = 0;
	for (int i = 0; i < 16; ++i) {
		char path[160], type[80];
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", i);
		if (!read_text(path, type, sizeof(type))) continue;
		// Do not label a battery/board sensor as CPU temperature.
		if (strcmp(type, "cpu") && strcmp(type, "cpu-thermal") &&
			strcmp(type, "soc") && strcmp(type, "soc-thermal")) continue;
		snprintf(cpu_thermal_path, sizeof(cpu_thermal_path), "/sys/class/thermal/thermal_zone%d/temp", i);
		break;
	}
}

static void refresh_cpu(void) {
	if (!cpu_label) return;
	char load[32] = "Sampling...", now[48] = "Unavailable";
	char range[72] = "Unavailable", governor[80] = "Unavailable";
	char governors[144] = "Unavailable", frequencies[200] = "Unavailable";
	char thermal[48] = "Unavailable", buf[256];
	unsigned long long total, idle;
	if (cpu_totals(&total, &idle)) {
		if (cpu_baseline && total > cpu_last_total && idle >= cpu_last_idle &&
			idle - cpu_last_idle <= total - cpu_last_total) {
			unsigned long long dt = total - cpu_last_total, di = idle - cpu_last_idle;
			snprintf(load, sizeof(load), "%u%%", (unsigned)((dt - di) * 100 / dt));
		}
		cpu_last_total = total;
		cpu_last_idle = idle;
		cpu_baseline = true;
	} else {
		cpu_baseline = false;
		snprintf(load, sizeof(load), "Unavailable");
	}
	long current = -1, minimum = -1, maximum = -1;
	if (frequency_text("scaling_cur_freq", buf, sizeof(buf))) current = positive_number(buf);
	if (current < 0 && frequency_text("cpuinfo_cur_freq", buf, sizeof(buf))) current = positive_number(buf);
	if (frequency_text("scaling_min_freq", buf, sizeof(buf))) minimum = positive_number(buf);
	if (frequency_text("scaling_max_freq", buf, sizeof(buf))) maximum = positive_number(buf);
	if (current > 0) snprintf(now, sizeof(now), "%ld MHz", current / 1000);
	if (minimum > 0 && maximum >= minimum) snprintf(range, sizeof(range), "%ld - %ld MHz", minimum / 1000, maximum / 1000);
	if (!frequency_text("scaling_governor", governor, sizeof(governor))) snprintf(governor, sizeof(governor), "Unavailable");
	if (!frequency_text("scaling_available_governors", governors, sizeof(governors))) snprintf(governors, sizeof(governors), "Unavailable");
	if (!frequency_text("scaling_available_frequencies", frequencies, sizeof(frequencies))) snprintf(frequencies, sizeof(frequencies), "Unavailable");
	if (cpu_thermal_path[0] && read_text(cpu_thermal_path, buf, sizeof(buf))) {
		char *end;
		long t = strtol(buf, &end, 10);
		if (end != buf && !*end && t >= -40000 && t <= 150000)
			snprintf(thermal, sizeof(thermal), "%ld C", t / 1000);
	}
	char report[800];
	snprintf(report, sizeof(report),
			 "CPU usage: %s\nCurrent frequency: %s\nPolicy range: %s\nGovernor: %s\n"
			 "Available governors: %s\nAvailable frequencies (kHz): %s\nCPU/SoC temperature: %s\nRead-only; stock clock policy retained",
			 load, now, range, governor, governors, frequencies, thermal);
	if (strcmp(lv_label_get_text(cpu_label), report)) lv_label_set_text(cpu_label, report);
}

static void cpu_timer_cb(lv_timer_t *timer) {
	if (lv_screen_active() != processespage_screen) {
		lv_timer_pause(timer);
		cpu_baseline = false;
		return;
	}
	if (!power_screen_is_on()) { cpu_baseline = false; return; }
	refresh_cpu();
}

// The process name (comm), newline stripped. False if the PID is already gone.
static bool read_comm(const char *pid, char *out, size_t size) {
	char path[300];
	snprintf(path, sizeof(path), "/proc/%s/comm", pid);
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	if (!fgets(out, (int)size, f)) {
		fclose(f);
		return false;
	}
	fclose(f);
	out[strcspn(out, "\n")] = '\0';
	return out[0] != '\0';
}

// Resident memory in kB, from /proc/PID/statm (second field, in pages). -1 if
// the PID is already gone.
static long read_rss_kb(const char *pid, long page_kb) {
	char path[300];
	snprintf(path, sizeof(path), "/proc/%s/statm", pid);
	FILE *f = fopen(path, "r");
	if (!f) {
		return -1;
	}
	long total_pages = 0, rss_pages = 0;
	int got = fscanf(f, "%ld %ld", &total_pages, &rss_pages);
	fclose(f);
	if (got < 2) {
		return -1;
	}
	return rss_pages * page_kb;
}

static int rss_desc(const void *a, const void *b) {
	long ra = ((const proc_row_t *)a)->rss_kb;
	long rb = ((const proc_row_t *)b)->rss_kb;
	return ra < rb ? 1 : ra > rb ? -1 : 0;
}

// "12,3 MB" above a megabyte, "820 kB" below: this device's RAM is measured in
// megabytes, but a small process must not read as "0 MB".
static void format_kb(long kb, char *out, size_t size) {
	if (kb < 0) {
		snprintf(out, size, "-");
	} else if (kb >= 1024) {
		snprintf(out, size, "%ld,%ld MB", kb / 1024, (kb % 1024) * 10 / 1024);
	} else {
		snprintf(out, size, "%ld kB", kb);
	}
}

// A list row: name on the left, memory on the right, like the other settings
// rows but with no chevron and no touch handling.
static void add_proc_row(const char *name, long rss_kb) {
	lv_obj_t *card = lv_obj_create(proc_list);
	lv_obj_set_width(card, lv_pct(100));
	lv_obj_set_height(card, LV_SIZE_CONTENT);
	lv_obj_add_style(card, &theme_style_card, 0);
	lv_obj_set_style_radius(card, 12, 0);
	lv_obj_set_style_border_width(card, 0, 0);
	lv_obj_set_style_shadow_width(card, 0, 0);
	lv_obj_set_style_pad_hor(card, 18, 0);
	lv_obj_set_style_pad_ver(card, 10, 0);
	lv_obj_set_scrollable(card, false);

	lv_obj_t *name_lbl = lv_label_create(card);
	lv_label_set_text(name_lbl, name);
	lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
	lv_obj_set_width(name_lbl, lv_pct(62));
	lv_obj_set_style_text_font(name_lbl, &font_ui_22, 0);
	lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 0, 0);

	char mem[24];
	format_kb(rss_kb, mem, sizeof(mem));
	lv_obj_t *mem_lbl = lv_label_create(card);
	lv_label_set_text(mem_lbl, mem);
	lv_obj_add_style(mem_lbl, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(mem_lbl, &font_ui_20, 0);
	lv_obj_align(mem_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
}

// Re-reads everything from /proc and redraws the RAM card and the list.
static void rebuild(void) {
	// --- RAM ---
	long total = meminfo_kb("MemTotal");
	long avail = meminfo_kb("MemAvailable");
	long freek = meminfo_kb("MemFree");
	if (avail < 0) {
		// Older kernels may not have MemAvailable (the 4.4 on this device does,
		// but not every build): fall back to free+buffers+cached.
		long buffers = meminfo_kb("Buffers");
		long cached = meminfo_kb("Cached");
		avail = (freek < 0 ? 0 : freek) + (buffers < 0 ? 0 : buffers) + (cached < 0 ? 0 : cached);
	}
	if (ram_label) {
		if (total > 0) {
			long used = total - avail;
			if (used < 0) {
				used = 0;
			}
			int pct = (int)(used * 100 / total);
			char used_s[24], total_s[24], avail_s[24];
			format_kb(used, used_s, sizeof(used_s));
			format_kb(total, total_s, sizeof(total_s));
			format_kb(avail, avail_s, sizeof(avail_s));
			// The player's own heap, on a third line. /proc says how much of the
			// process is anonymous but not what it is; this is the part of that
			// the interface and everything else asked malloc for, and watching
			// it while walking the pages is what tells a growth from a level.
			size_t heap_used = 0;
			heap_usage(&heap_used, NULL);
			char heap_s[24];
			format_kb((long)(heap_used / 1024), heap_s, sizeof(heap_s));
			lv_label_set_text_fmt(ram_label, "%s: %s / %s (%d%%)\n%s: %s\n%s: %s", tr("processes_ram_used"), used_s, total_s,
								  pct, tr("processes_ram_free"), avail_s, tr("processes_player_heap"), heap_s);
		} else {
			lv_label_set_text(ram_label, "/proc/meminfo?");
		}
	}

	// --- processes ---
	if (!proc_list) {
		return;
	}
	lv_obj_clean(proc_list); // drop the rows from the previous pass

	long page_kb = sysconf(_SC_PAGESIZE) / 1024;
	if (page_kb < 1) {
		page_kb = 4;
	}

	static proc_row_t rows[PROC_MAX];
	int n = 0;
	DIR *d = opendir("/proc");
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) != NULL && n < PROC_MAX) {
			// Only all-digit directory names: those are the PIDs.
			const char *p = e->d_name;
			bool numeric = p[0] != '\0';
			for (const char *c = p; *c; c++) {
				if (*c < '0' || *c > '9') {
					numeric = false;
					break;
				}
			}
			if (!numeric) {
				continue;
			}
			char name[24];
			if (!read_comm(p, name, sizeof(name))) {
				continue;
			}
			long rss = read_rss_kb(p, page_kb);
			rows[n].rss_kb = rss;
			snprintf(rows[n].name, sizeof(rows[n].name), "%s", name);
			n++;
		}
		closedir(d);
	}

	qsort(rows, (size_t)n, sizeof(rows[0]), rss_desc);

	int shown = n < PROC_SHOWN ? n : PROC_SHOWN;
	for (int i = 0; i < shown; i++) {
		add_proc_row(rows[i].name, rows[i].rss_kb);
	}
}

static void screen_loaded_cb(lv_event_t *e) {
	(void)e;
	cpu_baseline = false;
	find_cpu_thermal();
	refresh_cpu();
	rebuild();
	if (cpu_timer) { lv_timer_reset(cpu_timer); lv_timer_resume(cpu_timer); }
}

static void screen_unloaded_cb(lv_event_t *e) {
	(void)e;
	if (cpu_timer) lv_timer_pause(cpu_timer);
	cpu_baseline = false;
	if (proc_list) lv_obj_clean(proc_list);
}

// Refresh on demand: the page already redraws on every open, but watching a
// process grow should not require leaving and re-entering for each new figure.
static void refresh_clicked_cb(lv_event_t *e) {
	(void)e;
	find_cpu_thermal();
	refresh_cpu();
	rebuild();
}

void processespage_init(gui_config_t *cfg) {
	page_container = settingsrow_page(processespage_screen, cfg, "processes");

	// The top-right icon, like the other page corner buttons.
	settingsrow_title_corner_slots(settingsrow_page_title(processespage_screen), cfg, 1);
	{
		lv_obj_t *button = lv_btn_create(processespage_screen);
		lv_obj_set_size(button, 56, 56);
		lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(button, 0, 0);
		lv_obj_set_style_shadow_width(button, 0, 0);
		lv_obj_set_style_pad_all(button, 0, 0);
		lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -cfg->padding, cfg->padding + cfg->top_bar_height);
		lv_obj_add_event_cb(button, refresh_clicked_cb, LV_EVENT_CLICKED, NULL);

		lv_obj_t *icon = lv_image_create(button);
		lv_image_set_src(icon, &icon_refresh);
		lv_obj_add_style(icon, &theme_style_icon, 0);
		lv_obj_center(icon);
	}

	// The RAM card: two lines, used/total and free.
	lv_obj_t *ram_card = lv_obj_create(page_container);
	lv_obj_set_width(ram_card, lv_pct(100));
	lv_obj_set_height(ram_card, LV_SIZE_CONTENT);
	lv_obj_add_style(ram_card, &theme_style_card, 0);
	lv_obj_set_style_radius(ram_card, 12, 0);
	lv_obj_set_style_border_width(ram_card, 0, 0);
	lv_obj_set_style_shadow_width(ram_card, 0, 0);
	lv_obj_set_style_pad_all(ram_card, 18, 0);
	lv_obj_set_scrollable(ram_card, false);

	ram_label = lv_label_create(ram_card);
	lv_obj_set_width(ram_label, lv_pct(100));
	lv_obj_set_style_text_font(ram_label, &font_ui_22, 0);
	lv_label_set_text(ram_label, "");

	// CPU diagnostics, sampled only while this screen is visible.
	lv_obj_t *cpu_card = lv_obj_create(page_container);
	lv_obj_set_width(cpu_card, lv_pct(100));
	lv_obj_set_height(cpu_card, LV_SIZE_CONTENT);
	lv_obj_add_style(cpu_card, &theme_style_card, 0);
	lv_obj_set_style_radius(cpu_card, 12, 0);
	lv_obj_set_style_border_width(cpu_card, 0, 0);
	lv_obj_set_style_shadow_width(cpu_card, 0, 0);
	lv_obj_set_style_pad_all(cpu_card, 18, 0);
	lv_obj_set_scrollable(cpu_card, false);
	cpu_label = lv_label_create(cpu_card);
	lv_obj_set_width(cpu_label, lv_pct(100));
	lv_label_set_long_mode(cpu_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_text_font(cpu_label, &font_ui_20, 0);
	lv_label_set_text(cpu_label, "CPU diagnostics");
	cpu_timer = lv_timer_create(cpu_timer_cb, 2000, NULL);
	if (cpu_timer) { lv_timer_pause(cpu_timer); power_pause_in_standby(cpu_timer); }

	// The list heading, then the list itself: a column refilled on every open.
	lv_obj_t *list_title = lv_label_create(page_container);
	lv_label_set_text(list_title, tr("processes_running_processes"));
	lv_obj_add_style(list_title, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(list_title, &font_ui_18, 0);
	lv_obj_set_style_pad_top(list_title, 6, 0);

	proc_list = lv_obj_create(page_container);
	lv_obj_set_width(proc_list, lv_pct(100));
	lv_obj_set_height(proc_list, LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(proc_list, 0, 0);
	lv_obj_set_style_border_width(proc_list, 0, 0);
	lv_obj_set_style_pad_all(proc_list, 0, 0);
	lv_obj_set_style_pad_row(proc_list, 8, 0);
	lv_obj_set_flex_flow(proc_list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scrollable(proc_list, false);

	// Do not scan /proc or allocate up to 60 rows during ordinary boot.
	lv_obj_add_event_cb(processespage_screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
	lv_obj_add_event_cb(processespage_screen, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
}
