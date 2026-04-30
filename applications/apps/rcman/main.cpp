#define ROS_APP_WITH_CRT 1
#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_GDI 1

#include "app/app.h"
#include "console.h"

#include <cstring>
#include <stdarg.h>

#define RCMAN_WINDOW_CLASS "rcman.main"
#define RCMAN_WINDOW_TITLE "rcman - resource monitor"
#define RCMAN_WINDOW_X 24L
#define RCMAN_WINDOW_Y 24L
#define RCMAN_WINDOW_WIDTH 980UL
#define RCMAN_WINDOW_HEIGHT 680UL
#define RCMAN_REFRESH_MSEC 500UL
#define RCMAN_ENABLE_MODULE_SNAPSHOT 1
#define RCMAN_ENABLE_CONSOLE_DEBUG 1
#define RCMAN_VERBOSE_MODULE_DEBUG 0
#define RCMAN_TASK_SCAN_MAX 1024UL
#define RCMAN_MODULE_SCAN_MAX 64UL
#define RCMAN_MAX_LINES 512UL
#define RCMAN_LINE_CAPACITY 384UL
#define RCMAN_ROW_NAME_CAPACITY 72UL
#define RCMAN_ROW_PID_CAPACITY 12UL
#define RCMAN_ROW_STATE_CAPACITY 16UL
#define RCMAN_ROW_MEMORY_CAPACITY 20UL
#define RCMAN_ROW_DETAIL_CAPACITY 160UL
#define RCMAN_PANEL_Y 202UL
#define RCMAN_TABLE_HEADER_HEIGHT 30UL
#define RCMAN_TABLE_FOOTER_HEIGHT 24UL
#define RCMAN_TABLE_ROW_GAP 6UL
#define RCMAN_CONSOLE_KIND_WIDTH 5UL
#define RCMAN_CONSOLE_NAME_WIDTH 22UL
#define RCMAN_CONSOLE_PID_WIDTH 5UL
#define RCMAN_CONSOLE_PPID_WIDTH 5UL
#define RCMAN_CONSOLE_STATE_WIDTH 7UL
#define RCMAN_CONSOLE_MEMORY_WIDTH 10UL
#define RCMAN_UI_NAME_MAX_CHARS 18UL
#define RCMAN_UI_DETAIL_MAX_CHARS 52UL
#define RCMAN_UI_PATH_DETAIL_MAX_CHARS 66UL

#define RCMAN_BG_COLOR 0x00101622UL
#define RCMAN_HEADER_COLOR 0x00171F2DUL
#define RCMAN_PANEL_COLOR 0x00151E2AUL
#define RCMAN_PANEL_ALT_COLOR 0x001B2635UL
#define RCMAN_BORDER_COLOR 0x002D4258UL
#define RCMAN_TEXT_COLOR 0x00E7EEF5UL
#define RCMAN_MUTED_COLOR 0x0099ACC1UL
#define RCMAN_GREEN_COLOR 0x004ED38AUL
#define RCMAN_BLUE_COLOR 0x004FA7FFUL
#define RCMAN_AMBER_COLOR 0x00EAC15AUL
#define RCMAN_RED_COLOR 0x00EE7C7CUL

typedef struct RcmanTaskEntry {
    int present;
    UserTaskInfo task;
    UserTaskResourceInfo resource;
} RcmanTaskEntry;

typedef enum RcmanRowKind {
    RCMAN_ROW_KIND_PROCESS = 0,
    RCMAN_ROW_KIND_IMAGE,
    RCMAN_ROW_KIND_MODULE,
    RCMAN_ROW_KIND_SECTION,
    RCMAN_ROW_KIND_PATH,
    RCMAN_ROW_KIND_NOTE
} RcmanRowKind;

typedef struct RcmanDisplayRow {
    unsigned long kind;
    unsigned long depth;
    char name[RCMAN_ROW_NAME_CAPACITY];
    char pid[RCMAN_ROW_PID_CAPACITY];
    char parent[RCMAN_ROW_PID_CAPACITY];
    char state[RCMAN_ROW_STATE_CAPACITY];
    char memory[RCMAN_ROW_MEMORY_CAPACITY];
    char detail[RCMAN_ROW_DETAIL_CAPACITY];
} RcmanDisplayRow;

typedef struct RcmanSnapshot {
    UserMemInfo memory;
    RcmanTaskEntry tasks[RCMAN_TASK_SCAN_MAX];
    RcmanDisplayRow rows[RCMAN_MAX_LINES];
    unsigned long task_count;
    unsigned long row_count;
    unsigned long kernel_used_bytes;
    unsigned long refreshed_ms;
    long memory_status;
    long task_status;
    char header_line[RCMAN_LINE_CAPACITY];
    char subtitle_line[RCMAN_LINE_CAPACITY];
    char status_line[RCMAN_LINE_CAPACITY];
    char tree_lines[RCMAN_MAX_LINES][RCMAN_LINE_CAPACITY];
    unsigned long tree_line_count;
} RcmanSnapshot;

typedef struct RcmanWindowState {
    HWND hwnd;
    unsigned long timer_id;
    unsigned long window_width;
    unsigned long window_height;
    unsigned long scroll_row;
    unsigned long last_console_dump_hash;
    int console_snapshot_ready;
    int body_font_ready;
    int title_font_ready;
    RosGdiFont body_font;
    RosGdiFont title_font;
    RcmanSnapshot snapshot;
} RcmanWindowState;

static RcmanWindowState g_rcman = { 0 };

static void rcman_dump_console_snapshot(const RcmanSnapshot* snapshot);

/**
 * Map one task state to a compact label for the dashboard.
 *
 * @param state Kernel task state value.
 * @return Short ASCII label for the state.
 */
static const char* rcman_task_state_name(long state) {
    switch (state) {
    case USER_TASK_STATE_INITIALIZED:
        return "init";
    case USER_TASK_STATE_READY:
        return "ready";
    case USER_TASK_STATE_RUNNING:
        return "run";
    case USER_TASK_STATE_WAITING:
        return "wait";
    case USER_TASK_STATE_SUSPENDED:
        return "suspend";
    case USER_TASK_STATE_TERMINATED:
        return "dead";
    default:
        return "unknown";
    }
}

/**
 * Convert bytes to kibibytes for display.
 *
 * @param bytes Raw byte count.
 * @return Value rounded down to KiB.
 */
static unsigned long rcman_kib(unsigned long bytes) {
    return bytes / 1024UL;
}

/**
 * Copy one short ASCII string into fixed storage.
 *
 * @param destination Target buffer.
 * @param capacity Target capacity in bytes.
 * @param source Source string, or NULL.
 * @return Nothing.
 */
static void rcman_copy_text(char* destination, unsigned long capacity, const char* source) {
    unsigned long index = 0UL;

    if ((destination == 0) || (capacity == 0UL)) {
        return;
    }

    if (source == 0) {
        destination[0] = '\0';
        return;
    }

    while ((source[index] != '\0') && ((index + 1UL) < capacity)) {
        destination[index] = source[index];
        ++index;
    }

    destination[index] = '\0';
}

/**
 * Copy one string into fixed storage with an optional visible-width cap.
 *
 * The renderer has no clipping-aware text primitive, so UI and console column
 * formatting need a shared helper that truncates long values before drawing.
 *
 * @param destination Target buffer.
 * @param capacity Target capacity in bytes.
 * @param source Source string, or NULL.
 * @param max_chars Maximum visible characters to keep before termination.
 * @return Nothing.
 */
static void rcman_copy_text_limited(char* destination, unsigned long capacity, const char* source, unsigned long max_chars) {
    unsigned long source_length;
    unsigned long copy_length;

    if ((destination == 0) || (capacity == 0UL)) {
        return;
    }

    if (source == 0) {
        destination[0] = '\0';
        return;
    }

    source_length = (unsigned long)crt_strlen(source);
    copy_length = source_length;
    if (copy_length > max_chars) {
        copy_length = max_chars;
    }
    if (copy_length >= capacity) {
        copy_length = capacity - 1UL;
    }

    if ((source_length > copy_length) && (copy_length > 3UL)) {
        unsigned long index;

        for (index = 0UL; index < (copy_length - 3UL); ++index) {
            destination[index] = source[index];
        }
        destination[copy_length - 3UL] = '.';
        destination[copy_length - 2UL] = '.';
        destination[copy_length - 1UL] = '.';
        destination[copy_length] = '\0';
        return;
    }

    memcpy(destination, source, copy_length);
    destination[copy_length] = '\0';
}

/**
 * Append one single character into a bounded console row buffer.
 *
 * Manual append helpers avoid relying on width-format specifiers the tiny CRT
 * does not implement, which is why the old console output printed `%-5s` raw.
 *
 * @param destination Row buffer.
 * @param capacity Buffer capacity in bytes.
 * @param offset In-out write cursor.
 * @param value Character to append.
 * @return Nothing.
 */
static void rcman_append_console_char(char* destination, unsigned long capacity, unsigned long* offset, char value) {
    if ((destination == 0) || (offset == 0) || (*offset + 1UL) >= capacity) {
        return;
    }

    destination[*offset] = value;
    ++(*offset);
    destination[*offset] = '\0';
}

/**
 * Append one padded fixed-width console cell.
 *
 * @param destination Row buffer.
 * @param capacity Buffer capacity in bytes.
 * @param offset In-out write cursor.
 * @param text Cell text.
 * @param width Desired visible width.
 * @return Nothing.
 */
static void rcman_append_console_cell(char* destination, unsigned long capacity, unsigned long* offset, const char* text, unsigned long width) {
    unsigned long index = 0UL;

    if ((destination == 0) || (offset == 0)) {
        return;
    }

    while ((index < width) && (text != 0) && (text[index] != '\0') && ((*offset + 1UL) < capacity)) {
        destination[*offset] = text[index];
        ++(*offset);
        ++index;
    }
    while ((index < width) && ((*offset + 1UL) < capacity)) {
        destination[*offset] = ' ';
        ++(*offset);
        ++index;
    }
    destination[*offset] = '\0';
}

/**
 * Build one formatted console header or data line from explicit column widths.
 *
 * @param destination Row buffer.
 * @param capacity Buffer capacity in bytes.
 * @param kind Kind column text.
 * @param name Name column text.
 * @param pid PID column text.
 * @param parent Parent PID column text.
 * @param state State column text.
 * @param memory Memory column text.
 * @param detail Detail column text.
 * @return Nothing.
 */
static void rcman_format_console_columns(
    char* destination,
    unsigned long capacity,
    const char* kind,
    const char* name,
    const char* pid,
    const char* parent,
    const char* state,
    const char* memory,
    const char* detail) {
    unsigned long offset = 0UL;

    if ((destination == 0) || (capacity == 0UL)) {
        return;
    }

    destination[0] = '\0';
    rcman_append_console_cell(destination, capacity, &offset, kind ? kind : "", RCMAN_CONSOLE_KIND_WIDTH);
    rcman_append_console_char(destination, capacity, &offset, ' ');
    rcman_append_console_cell(destination, capacity, &offset, name ? name : "", RCMAN_CONSOLE_NAME_WIDTH);
    rcman_append_console_char(destination, capacity, &offset, ' ');
    rcman_append_console_cell(destination, capacity, &offset, pid ? pid : "", RCMAN_CONSOLE_PID_WIDTH);
    rcman_append_console_char(destination, capacity, &offset, ' ');
    rcman_append_console_cell(destination, capacity, &offset, parent ? parent : "", RCMAN_CONSOLE_PPID_WIDTH);
    rcman_append_console_char(destination, capacity, &offset, ' ');
    rcman_append_console_cell(destination, capacity, &offset, state ? state : "", RCMAN_CONSOLE_STATE_WIDTH);
    rcman_append_console_char(destination, capacity, &offset, ' ');
    rcman_append_console_cell(destination, capacity, &offset, memory ? memory : "", RCMAN_CONSOLE_MEMORY_WIDTH);
    rcman_append_console_char(destination, capacity, &offset, ' ');
    rcman_copy_text(destination + offset, capacity - offset, detail ? detail : "");
}

/**
 * Format one short KiB value for table cells.
 *
 * The GUI and console share one compact memory column, so pre-formatting the
 * value keeps both render paths aligned and avoids repeating width logic at
 * draw time.
 *
 * @param destination Target buffer.
 * @param capacity Target capacity in bytes.
 * @param kib_value Value already converted to KiB.
 * @return Nothing.
 */
static void rcman_format_kib_cell(char* destination, unsigned long capacity, unsigned long kib_value) {
    if ((destination == 0) || (capacity == 0UL)) {
        return;
    }

    (void)crt_snprintf(destination, capacity, "%lu KB", kib_value);
}

/**
 * Build one indented label for table and console name columns.
 *
 * Keeping indentation out of the stored row text lets the GUI indent with
 * pixels while the console formatter still emits a readable hierarchy.
 *
 * @param row Source display row.
 * @param destination Target buffer.
 * @param capacity Target capacity in bytes.
 * @return Nothing.
 */
static void rcman_format_indented_name(const RcmanDisplayRow* row, char* destination, unsigned long capacity) {
    unsigned long index = 0UL;
    unsigned long depth;

    if ((row == 0) || (destination == 0) || (capacity == 0UL)) {
        return;
    }

    depth = row->depth;
    if (depth > 10UL) {
        depth = 10UL;
    }

    while ((depth > 0UL) && ((index + 2UL) < capacity)) {
        destination[index++] = ' ';
        destination[index++] = ' ';
        --depth;
    }

    destination[index] = '\0';
    rcman_copy_text(destination + index, capacity - index, row->name);
}

/**
 * Render one display row into the aligned console mirror format.
 *
 * The console path is still valuable when GUI startup or repainting fails, so
 * it now uses the same structured rows as the GUI rather than dumping the old
 * free-form tree strings.
 *
 * @param row Source display row.
 * @param destination Target text buffer.
 * @param capacity Target capacity in bytes.
 * @return Nothing.
 */
static void rcman_format_console_row(const RcmanDisplayRow* row, char* destination, unsigned long capacity) {
    char kind[8];
    char indented_name[RCMAN_ROW_NAME_CAPACITY + 24UL];

    if ((row == 0) || (destination == 0) || (capacity == 0UL)) {
        return;
    }

    switch ((RcmanRowKind)row->kind) {
    case RCMAN_ROW_KIND_PROCESS:
        rcman_copy_text(kind, sizeof(kind), "PROC");
        break;
    case RCMAN_ROW_KIND_IMAGE:
        rcman_copy_text(kind, sizeof(kind), "IMG");
        break;
    case RCMAN_ROW_KIND_MODULE:
        rcman_copy_text(kind, sizeof(kind), "DLL");
        break;
    case RCMAN_ROW_KIND_SECTION:
        rcman_copy_text(kind, sizeof(kind), "SEC");
        break;
    case RCMAN_ROW_KIND_PATH:
        rcman_copy_text(kind, sizeof(kind), "PATH");
        break;
    default:
        rcman_copy_text(kind, sizeof(kind), "NOTE");
        break;
    }

    rcman_format_indented_name(row, indented_name, sizeof(indented_name));
    rcman_format_console_columns(
        destination,
        capacity,
        kind,
        indented_name,
        row->pid,
        row->parent,
        row->state,
        row->memory,
        row->detail);
}

/**
 * Append one structured row and keep the console mirror text in sync.
 *
 * rcman now renders from explicit columns, but the change-only console hash
 * still depends on a stable textual projection of those same rows.
 *
 * @param snapshot Snapshot being built.
 * @param kind Row category.
 * @param depth Hierarchy depth for indentation.
 * @param name Primary label shown in the first wide column.
 * @param pid PID column text.
 * @param parent Parent PID column text.
 * @param state State column text.
 * @param memory Memory column text.
 * @param detail Trailing detail column text.
 * @return Nothing.
 */
static void rcman_add_display_row(
    RcmanSnapshot* snapshot,
    RcmanRowKind kind,
    unsigned long depth,
    const char* name,
    const char* pid,
    const char* parent,
    const char* state,
    const char* memory,
    const char* detail) {
    RcmanDisplayRow* row;

    if ((snapshot == 0)
        || (snapshot->row_count >= COUNT_OF(snapshot->rows))
        || (snapshot->tree_line_count >= COUNT_OF(snapshot->tree_lines))) {
        return;
    }

    row = &snapshot->rows[snapshot->row_count];
    memset(row, 0, sizeof(*row));
    row->kind = (unsigned long)kind;
    row->depth = depth;
    rcman_copy_text(row->name, sizeof(row->name), name ? name : "");
    rcman_copy_text(row->pid, sizeof(row->pid), pid ? pid : "");
    rcman_copy_text(row->parent, sizeof(row->parent), parent ? parent : "");
    rcman_copy_text(row->state, sizeof(row->state), state ? state : "");
    rcman_copy_text(row->memory, sizeof(row->memory), memory ? memory : "");
    rcman_copy_text(row->detail, sizeof(row->detail), detail ? detail : "");
    rcman_format_console_row(row, snapshot->tree_lines[snapshot->tree_line_count], RCMAN_LINE_CAPACITY);
    ++snapshot->row_count;
    ++snapshot->tree_line_count;
}

/**
 * Map one process state label to the accent color used in both table views.
 *
 * Matching the GUI state chip color and the console row color makes the serial
 * fallback easier to compare against the on-screen table.
 *
 * @param state Short state label.
 * @return RGB color for GUI text accents.
 */
static unsigned long rcman_state_color(const char* state) {
    if (state == 0) {
        return RCMAN_MUTED_COLOR;
    }

    if (0 == crt_strcmp(state, "run")) {
        return RCMAN_GREEN_COLOR;
    }
    if (0 == crt_strcmp(state, "ready")) {
        return RCMAN_BLUE_COLOR;
    }
    if (0 == crt_strcmp(state, "wait")) {
        return RCMAN_AMBER_COLOR;
    }
    if ((0 == crt_strcmp(state, "dead")) || (0 == crt_strcmp(state, "error"))) {
        return RCMAN_RED_COLOR;
    }
    return RCMAN_MUTED_COLOR;
}

/**
 * Choose one primary row accent color for the GUI table.
 *
 * Different row categories need slightly different emphasis so process rows,
 * library rows, and auxiliary notes do not visually collapse together.
 *
 * @param row Row being rendered.
 * @return RGB color for the row accent and label text.
 */
static unsigned long rcman_row_accent_color(const RcmanDisplayRow* row) {
    if (row == 0) {
        return RCMAN_MUTED_COLOR;
    }

    switch ((RcmanRowKind)row->kind) {
    case RCMAN_ROW_KIND_PROCESS:
        return rcman_state_color(row->state);
    case RCMAN_ROW_KIND_IMAGE:
        return RCMAN_BLUE_COLOR;
    case RCMAN_ROW_KIND_MODULE:
        return RCMAN_BLUE_COLOR;
    case RCMAN_ROW_KIND_SECTION:
        return RCMAN_MUTED_COLOR;
    case RCMAN_ROW_KIND_PATH:
        return RCMAN_MUTED_COLOR;
    default:
        return RCMAN_AMBER_COLOR;
    }
}

/**
 * Pick one zebra-strip background color for the current table row.
 *
 * Alternating fills improve scanability, while note rows stay slightly darker
 * so they read like annotations rather than primary process records.
 *
 * @param row_index Visible row index.
 * @param row Row metadata.
 * @return RGB background color.
 */
static unsigned long rcman_row_background_color(unsigned long row_index, const RcmanDisplayRow* row) {
    if ((row != 0) && ((RcmanRowKind)row->kind == RCMAN_ROW_KIND_NOTE)) {
        return RCMAN_PANEL_COLOR;
    }

    return ((row_index & 1UL) == 0UL) ? RCMAN_PANEL_ALT_COLOR : RCMAN_PANEL_COLOR;
}

/**
 * Compute how many body rows fit in the current window height.
 *
 * Scroll handling needs the same capacity calculation as painting so page-up
 * and page-down move by one actual viewport rather than a guessed number.
 *
 * @param state Window state whose client size and font are already known.
 * @return Number of fully visible body rows.
 */
static unsigned long rcman_visible_row_capacity(const RcmanWindowState* state) {
    unsigned long line_height;
    unsigned long row_height;
    unsigned long body_top;
    unsigned long body_bottom;

    if (state == 0) {
        return 0UL;
    }

    line_height = state->body_font.line_height != 0UL ? state->body_font.line_height : 18UL;
    row_height = line_height + RCMAN_TABLE_ROW_GAP;
    body_top = RCMAN_PANEL_Y + RCMAN_TABLE_HEADER_HEIGHT + 18UL;
    body_bottom = state->window_height > (RCMAN_TABLE_FOOTER_HEIGHT + 18UL)
        ? (state->window_height - RCMAN_TABLE_FOOTER_HEIGHT - 18UL)
        : 0UL;

    if ((row_height == 0UL) || (body_bottom <= body_top)) {
        return 0UL;
    }

    return (body_bottom - body_top) / row_height;
}

/**
 * Return the highest legal scroll origin for the current row count.
 *
 * @param total_rows Total rows available in the snapshot.
 * @param visible_rows Number of rows currently visible.
 * @return Maximum top-row index.
 */
static unsigned long rcman_max_scroll_row(unsigned long total_rows, unsigned long visible_rows) {
    if ((visible_rows == 0UL) || (total_rows <= visible_rows)) {
        return 0UL;
    }

    return total_rows - visible_rows;
}

/**
 * Clamp the current scroll origin so it remains valid after resize or refresh.
 *
 * @param state Window state holding the scroll origin.
 * @param visible_rows Number of rows visible in the current viewport.
 * @return Nothing.
 */
static void rcman_clamp_scroll_row(RcmanWindowState* state, unsigned long visible_rows) {
    unsigned long max_scroll;

    if (state == 0) {
        return;
    }

    max_scroll = rcman_max_scroll_row(state->snapshot.row_count, visible_rows);
    if (state->scroll_row > max_scroll) {
        state->scroll_row = max_scroll;
    }
}

/**
 * Move the viewport by one signed row delta.
 *
 * @param state Window state holding the current scroll origin.
 * @param delta Signed row delta.
 * @param visible_rows Number of rows visible in the current viewport.
 * @return Non-zero when the scroll origin changed.
 */
static int rcman_scroll_rows(RcmanWindowState* state, long delta, unsigned long visible_rows) {
    long next_scroll;
    unsigned long max_scroll;

    if (state == 0) {
        return 0;
    }

    max_scroll = rcman_max_scroll_row(state->snapshot.row_count, visible_rows);
    next_scroll = (long)state->scroll_row + delta;
    if (next_scroll < 0L) {
        next_scroll = 0L;
    }
    if ((unsigned long)next_scroll > max_scroll) {
        next_scroll = (long)max_scroll;
    }
    if ((unsigned long)next_scroll == state->scroll_row) {
        return 0;
    }

    state->scroll_row = (unsigned long)next_scroll;
    return 1;
}

/**
 * Hash one snapshot into a compact console-dump signature.
 *
 * The console mirror is useful for debugging, but dumping every timer tick is
 * too noisy. A lightweight hash lets rcman print again only when the visible
 * snapshot content actually changes.
 *
 * @param snapshot Snapshot to summarize.
 * @return Stable hash of the current console-visible snapshot content.
 */
static unsigned long rcman_snapshot_console_hash(const RcmanSnapshot* snapshot) {
    unsigned long hash = 2166136261UL;
    unsigned long line_index;
    unsigned long char_index;

    if (snapshot == 0) {
        return 0UL;
    }

    for (char_index = 0UL; snapshot->status_line[char_index] != '\0'; ++char_index) {
        hash ^= (unsigned long)(unsigned char)snapshot->status_line[char_index];
        hash *= 16777619UL;
    }

    hash ^= snapshot->task_count;
    hash *= 16777619UL;
    hash ^= snapshot->kernel_used_bytes;
    hash *= 16777619UL;

    for (line_index = 0UL; line_index < snapshot->tree_line_count; ++line_index) {
        for (char_index = 0UL; snapshot->tree_lines[line_index][char_index] != '\0'; ++char_index) {
            hash ^= (unsigned long)(unsigned char)snapshot->tree_lines[line_index][char_index];
            hash *= 16777619UL;
        }
        hash ^= 0xFFUL;
        hash *= 16777619UL;
    }

    return hash;
}

/**
 * Return the last path component of one task or module name.
 *
 * Loader and task metadata may carry either a short name or a DOS-style full
 * path. Reducing that to the basename keeps the tree compact and makes the
 * first token in each row visually useful.
 *
 * @param text Source text, or NULL.
 * @return Pointer to the basename inside the original string.
 */
static const char* rcman_basename(const char* text) {
    const char* cursor;
    const char* basename;

    if (text == 0) {
        return "";
    }

    basename = text;
    for (cursor = text; *cursor != '\0'; ++cursor) {
        if ((*cursor == '\\') || (*cursor == '/')) {
            basename = cursor + 1;
        }
    }

    return basename;
}

/**
 * Build one human-readable process label for GUI and console output.
 *
 * Putting the recognizable name first makes the process tree easier to scan
 * than trailing `name=` fields, especially once the row also carries PID and
 * memory totals.
 *
 * @param entry Process snapshot entry.
 * @param destination Target buffer.
 * @param capacity Target capacity in bytes.
 * @return Nothing.
 */
static void rcman_format_process_name(const RcmanTaskEntry* entry, char* destination, unsigned long capacity) {
    const char* raw_name;
    const char* short_name;

    if ((destination == 0) || (capacity == 0UL)) {
        return;
    }

    raw_name = ((entry != 0) && (entry->task.name[0] != '\0')) ? entry->task.name : "(unnamed)";
    short_name = rcman_basename(raw_name);
    if (short_name[0] == '\0') {
        short_name = raw_name;
    }

    (void)crt_snprintf(destination, capacity, "%s", short_name);
}

/**
 * Pick one readable display name for the process executable image row.
 *
 * rcman now shows the main image separately from dependent DLLs. Prefer the
 * executable path basename when the kernel supplied one, and fall back to the
 * process name otherwise so every process still gets a stable image label.
 *
 * @param entry Process snapshot that owns the executable image.
 * @param destination Target output buffer.
 * @param capacity Target capacity in bytes.
 * @return Nothing.
 */
static void rcman_format_image_name(const RcmanTaskEntry* entry, char* destination, unsigned long capacity) {
    const char* raw_name = 0;
    const char* short_name = 0;

    if ((destination == 0) || (capacity == 0UL)) {
        return;
    }

    if ((entry != 0) && (entry->resource.image_path[0] != '\0')) {
        raw_name = entry->resource.image_path;
    }
    else if ((entry != 0) && (entry->task.name[0] != '\0')) {
        raw_name = entry->task.name;
    }
    else {
        raw_name = "image";
    }

    short_name = rcman_basename(raw_name);
    if ((short_name == 0) || (short_name[0] == '\0')) {
        short_name = raw_name;
    }

    (void)crt_snprintf(destination, capacity, "%s", short_name);
}

/**
 * Convert raw section flags into a short permission string for the table view.
 *
 * The section subtree should make it immediately obvious whether one range is
 * executable, writable, or BSS-backed, so rcman renders the same compact flag
 * token the loader logs use.
 *
 * @param flags Raw `USER_TASK_SECTION_FLAG_*` bitmask.
 * @param destination Target output buffer.
 * @param capacity Target capacity in bytes.
 * @return Nothing.
 */
static void rcman_format_section_flags(unsigned long flags, char* destination, unsigned long capacity) {
    unsigned long index = 0UL;

    if ((destination == 0) || (capacity == 0UL)) {
        return;
    }

    if ((index + 1UL) < capacity) {
        destination[index++] = ((flags & USER_TASK_SECTION_FLAG_READ) != 0UL) ? 'R' : '-';
    }
    if ((index + 1UL) < capacity) {
        destination[index++] = ((flags & USER_TASK_SECTION_FLAG_WRITE) != 0UL) ? 'W' : '-';
    }
    if ((index + 1UL) < capacity) {
        destination[index++] = ((flags & USER_TASK_SECTION_FLAG_EXEC) != 0UL) ? 'X' : '-';
    }
    if (((flags & USER_TASK_SECTION_FLAG_BSS) != 0UL) && ((index + 1UL) < capacity)) {
        destination[index++] = 'B';
    }

    destination[index] = '\0';
}

/**
 * Return one semantic section-kind label for rcman detail text.
 *
 * Raw permission bits are useful but not descriptive enough when scanning the
 * process tree. Mapping them to code/rodata/global buckets makes the section
 * subtree easier to understand at a glance.
 *
 * @param flags Raw `USER_TASK_SECTION_FLAG_*` bitmask.
 * @return Static label describing the section role.
 */
static const char* rcman_section_kind_name(unsigned long flags) {
    if ((flags & USER_TASK_SECTION_FLAG_EXEC) != 0UL) {
        return "code";
    }
    if ((flags & USER_TASK_SECTION_FLAG_WRITE) != 0UL) {
        if ((flags & USER_TASK_SECTION_FLAG_BSS) != 0UL) {
            return "global-bss";
        }

        return "global-data";
    }

    return "rodata";
}

typedef enum RcmanSectionBucket {
    RCMAN_SECTION_BUCKET_TEXT = 0,
    RCMAN_SECTION_BUCKET_RODATA,
    RCMAN_SECTION_BUCKET_DATA,
    RCMAN_SECTION_BUCKET_BSS,
    RCMAN_SECTION_BUCKET_COUNT
} RcmanSectionBucket;

typedef struct RcmanSectionBucketSummary {
    int present;
    unsigned long start_address;
    unsigned long end_address;
    unsigned long total_bytes;
    unsigned long flags;
} RcmanSectionBucketSummary;

/**
 * Classify one raw section into the compact rcman display buckets.
 *
 * The ELF-like images contain many loader-oriented metadata sections that are
 * technically useful but too noisy for the process tree. Grouping them into the
 * same user-facing buckets keeps the display focused on the ranges people
 * actually care about: code, read-only data, writable data, and BSS.
 *
 * @param flags Raw `USER_TASK_SECTION_FLAG_*` bitmask.
 * @return Bucket identifier used for collapsed display rows.
 */
static RcmanSectionBucket rcman_section_bucket_for_flags(unsigned long flags) {
    if ((flags & USER_TASK_SECTION_FLAG_EXEC) != 0UL) {
        return RCMAN_SECTION_BUCKET_TEXT;
    }
    if ((flags & USER_TASK_SECTION_FLAG_WRITE) != 0UL) {
        if ((flags & USER_TASK_SECTION_FLAG_BSS) != 0UL) {
            return RCMAN_SECTION_BUCKET_BSS;
        }

        return RCMAN_SECTION_BUCKET_DATA;
    }

    return RCMAN_SECTION_BUCKET_RODATA;
}

/**
 * Return the display label for one collapsed section bucket.
 *
 * Even when several low-level sections are merged together, rcman should still
 * present familiar names that match how developers think about image layout.
 *
 * @param bucket Collapsed bucket identifier.
 * @return Short label for the tree row.
 */
static const char* rcman_section_bucket_name(RcmanSectionBucket bucket) {
    switch (bucket) {
    case RCMAN_SECTION_BUCKET_TEXT:
        return "text";
    case RCMAN_SECTION_BUCKET_RODATA:
        return "rodata";
    case RCMAN_SECTION_BUCKET_DATA:
        return "data";
    case RCMAN_SECTION_BUCKET_BSS:
        return "bss";
    default:
        return "(section)";
    }
}

/**
 * Return the canonical section label used in collapsed detail text.
 *
 * The console name column is intentionally narrow. Repeating the familiar
 * bucket label inside the detail text ensures the important category stays
 * visible even when the row is inspected outside the GUI table.
 *
 * @param bucket Collapsed bucket identifier.
 * @return Stable label for the collapsed bucket.
 */
static const char* rcman_section_bucket_detail_name(RcmanSectionBucket bucket) {
    switch (bucket) {
    case RCMAN_SECTION_BUCKET_TEXT:
        return ".text";
    case RCMAN_SECTION_BUCKET_RODATA:
        return ".rodata";
    case RCMAN_SECTION_BUCKET_DATA:
        return ".data";
    case RCMAN_SECTION_BUCKET_BSS:
        return ".bss";
    default:
        return "(section)";
    }
}

/**
 * Append one collapsed section-layout subtree beneath a process or DLL row.
 *
 * The raw image format exposes many tiny sections like `.hash`, `.dynsym`, and
 * `.got.plt`. rcman intentionally collapses those into a small number of buckets
 * so the tree answers the practical question of where code, rodata, writable
 * globals, and BSS landed in the target process.
 *
 * @param snapshot Snapshot being built.
 * @param sections Section snapshot array.
 * @param section_count Number of valid section entries.
 * @param depth Tree depth for the first section row.
 * @return Nothing.
 */
static void rcman_add_section_rows(
    RcmanSnapshot* snapshot,
    const UserTaskSectionInfo* sections,
    unsigned long section_count,
    unsigned long depth) {
    RcmanSectionBucketSummary buckets[RCMAN_SECTION_BUCKET_COUNT];
    unsigned long index;

    if ((snapshot == 0) || ((sections == 0) && (section_count != 0UL))) {
        return;
    }

    memset(buckets, 0, sizeof(buckets));

    for (index = 0UL; index < section_count; ++index) {
        const UserTaskSectionInfo* section = &sections[index];
        RcmanSectionBucketSummary* bucket;
        RcmanSectionBucket bucket_kind;
        unsigned long section_bytes = 0UL;

        if (section->end_address <= section->start_address) {
            continue;
        }

        section_bytes = section->end_address - section->start_address;
        bucket_kind = rcman_section_bucket_for_flags(section->flags);
        bucket = &buckets[(unsigned long)bucket_kind];

        if (bucket->present == 0) {
            bucket->present = 1;
            bucket->start_address = section->start_address;
            bucket->end_address = section->end_address;
        }
        else {
            if (section->start_address < bucket->start_address) {
                bucket->start_address = section->start_address;
            }
            if (section->end_address > bucket->end_address) {
                bucket->end_address = section->end_address;
            }
        }

        bucket->total_bytes += section_bytes;
        bucket->flags |= section->flags;
    }

    for (index = 0UL; index < RCMAN_SECTION_BUCKET_COUNT; ++index) {
        const RcmanSectionBucketSummary* bucket = &buckets[index];
        char memory[RCMAN_ROW_MEMORY_CAPACITY];
        char state[RCMAN_ROW_STATE_CAPACITY];
        char detail[RCMAN_ROW_DETAIL_CAPACITY];

        if (bucket->present == 0) {
            continue;
        }

        rcman_format_kib_cell(memory, sizeof(memory), rcman_kib(bucket->total_bytes));
        rcman_format_section_flags(bucket->flags, state, sizeof(state));
        (void)crt_snprintf(
            detail,
            sizeof(detail),
            "%s 0x%lx - 0x%lx %s",
            rcman_section_bucket_detail_name((RcmanSectionBucket)index),
            bucket->start_address,
            bucket->end_address,
            rcman_section_kind_name(bucket->flags));
        rcman_add_display_row(
            snapshot,
            RCMAN_ROW_KIND_SECTION,
            depth,
            rcman_section_bucket_name((RcmanSectionBucket)index),
            "",
            "",
            state,
            memory,
            detail);
    }
}

/**
 * Append the main executable image subtree for one process.
 *
 * The process row keeps aggregate memory figures, while this subtree exposes
 * the actual executable mapping base, path, and section address ranges that the
 * user asked to inspect.
 *
 * @param snapshot Snapshot being built.
 * @param entry Process entry that owns the executable image.
 * @param depth Tree depth of the process row.
 * @return Nothing.
 */
static void rcman_add_process_image_rows(RcmanSnapshot* snapshot, const RcmanTaskEntry* entry, unsigned long depth) {
    if ((snapshot == 0) || (entry == 0)) {
        return;
    }
    if ((entry->resource.image_bytes == 0UL)
        && (entry->resource.image_section_count == 0UL)
        && (entry->resource.image_path[0] == '\0')) {
        return;
    }

    rcman_add_section_rows(snapshot, entry->resource.image_sections, entry->resource.image_section_count, depth + 1UL);

    if ((entry->resource.flags & USER_TASK_RESOURCE_FLAG_IMAGE_SECTION_TRUNCATED) != 0UL) {
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, depth + 1UL, "sections", "", "", "warn", "", "image section rows truncated");
    }
    if (entry->resource.image_path[0] != '\0') {
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_PATH, depth + 1UL, "path", "", "", "", "", entry->resource.image_path);
    }
}

/**
 * Mirror one module-query result to the console for live debugging.
 *
 * Library discovery currently needs both GUI visibility and a serial/console
 * breadcrumb trail so backend failures can be correlated with the refresh that
 * triggered them.
 *
 * @param entry Process whose modules were queried.
 * @param status Syscall return status.
 * @param module_count Number of returned modules.
 * @param modules Module buffer when entries were returned.
 * @param capacity Number of valid slots in `modules`.
 * @return Nothing.
 */
static void rcman_log_module_query(
    const RcmanTaskEntry* entry,
    long status,
    unsigned long module_count,
    const UserTaskModuleInfo* modules,
    unsigned long capacity) {
#if !RCMAN_VERBOSE_MODULE_DEBUG
    (void)entry;
    (void)status;
    (void)module_count;
    (void)modules;
    (void)capacity;
#else
    char process_name[64];
    unsigned long index;

    if (entry == 0) {
        return;
    }

    rcman_format_process_name(entry, process_name, sizeof(process_name));
    crt_printf(
        "rcman.exe: module-scan pid=%ld name=%s state=%s status=%ld count=%lu\r\n",
        entry->task.id,
        process_name,
        rcman_task_state_name(entry->task.main_thread_state),
        status,
        module_count);

    for (index = 0UL; (index < module_count) && (index < capacity) && (modules != 0); ++index) {
        const UserTaskModuleInfo* module = &modules[index];
        const char* module_name = (module->module_name[0] != '\0') ? rcman_basename(module->module_name) : "(unnamed)";

        crt_printf(
            "  module[%lu]: %s base=0x%lx image=%luKB shared=%luKB private=%luKB ref=%lu flags=0x%lx sections=%lu\r\n",
            index,
            module_name,
            module->image_base,
            rcman_kib(module->image_bytes),
            rcman_kib(module->shared_backing_bytes),
            rcman_kib(module->private_backing_bytes),
            module->shared_reference_count,
            module->flags,
            module->section_count);
        if (module->path[0] != '\0') {
            crt_printf("    path=%s\r\n", module->path);
        }
    }
#endif
}

/**
 * Append one formatted line to the snapshot tree buffer.
 *
 * @param snapshot Snapshot being built.
 * @param fmt Printf-style format string.
 * @param ... Format arguments.
 * @return Nothing.
 */
 /**
  * Append the loaded-module subtree for one process.
  *
  * @param snapshot Snapshot being built.
  * @param entry Task entry whose libraries should be shown.
  * @param depth Tree depth for the process row.
  * @return Nothing.
  */
static void rcman_add_module_rows(RcmanSnapshot* snapshot, const RcmanTaskEntry* entry, unsigned long depth) {
#if !RCMAN_ENABLE_MODULE_SNAPSHOT
    static int module_snapshot_warning_emitted = 0;

    if (RCMAN_ENABLE_CONSOLE_DEBUG && (module_snapshot_warning_emitted == 0)) {
        crt_printf("rcman.exe: library snapshot disabled because DllLoader::snapshot_process_modules still corrupts the loader heap\r\n");
        module_snapshot_warning_emitted = 1;
    }

    (void)entry;
    rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, depth + 1UL, "libraries", "", "", "error", "", "snapshot backend disabled");
    return;
#else
    UserTaskModuleInfo modules[RCMAN_MODULE_SCAN_MAX];
    unsigned long module_count = 0UL;
    unsigned long module_limit = COUNT_OF(modules);
    unsigned long index;
    long module_status;

    if ((snapshot == 0) || (entry == 0)) {
        return;
    }

    if ((entry->task.main_thread_state != USER_TASK_STATE_RUNNING)
        && (entry->task.main_thread_state != USER_TASK_STATE_WAITING)) {
        rcman_log_module_query(entry, 0L, 0UL, 0, 0UL);
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, depth + 1UL, "libraries", "", "", "wait", "", "pending task startup");
        return;
    }

    module_status = getTaskModuleInfo(entry->task.id, modules, module_limit, &module_count);
    rcman_log_module_query(entry, module_status, module_count, modules, module_limit);
    if ((module_status < 0L) && (module_count == 0UL)) {
        char detail[RCMAN_ROW_DETAIL_CAPACITY];

        (void)crt_snprintf(detail, sizeof(detail), "unavailable status=%ld", module_status);
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, depth + 1UL, "libraries", "", "", "error", "", detail);
        return;
    }

    if (module_count == 0UL) {
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, depth + 1UL, "libraries", "", "", "idle", "", "none reported");
        return;
    }

    if (module_status < 0L) {
        char detail[RCMAN_ROW_DETAIL_CAPACITY];

        (void)crt_snprintf(detail, sizeof(detail), "partial status=%ld", module_status);
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, depth + 1UL, "libraries", "", "", "warn", "", detail);
    }

    for (index = 0UL; (index < module_count) && (index < module_limit); ++index) {
        const UserTaskModuleInfo* module = &modules[index];
        char memory[RCMAN_ROW_MEMORY_CAPACITY];
        char detail[RCMAN_ROW_DETAIL_CAPACITY];
        const char* module_name = (module->module_name[0] != '\0') ? rcman_basename(module->module_name) : "(unnamed)";

        rcman_format_kib_cell(memory, sizeof(memory), rcman_kib(module->image_bytes));
        (void)crt_snprintf(
            detail,
            sizeof(detail),
            "base=0x%lx shared=%lu KB private=%lu KB ref=%lu sections=%lu%s",
            module->image_base,
            rcman_kib(module->shared_backing_bytes),
            rcman_kib(module->private_backing_bytes),
            module->shared_reference_count,
            module->section_count,
            ((module->flags & USER_TASK_MODULE_FLAG_SECTION_TRUNCATED) != 0UL) ? "+" : "");
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_MODULE, depth + 1UL, module_name, "", "", "dll", memory, detail);
        rcman_add_section_rows(snapshot, module->sections, module->section_count, depth + 2UL);

        if ((module->flags & USER_TASK_MODULE_FLAG_SECTION_TRUNCATED) != 0UL) {
            rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, depth + 2UL, "sections", "", "", "warn", "", "module section rows truncated");
        }

        if (module->path[0] != '\0') {
            rcman_add_display_row(snapshot, RCMAN_ROW_KIND_PATH, depth + 2UL, "path", "", "", "", "", module->path);
        }
    }

    if (module_count >= module_limit) {
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, depth + 1UL, "libraries", "", "", "warn", "", "module rows truncated");
    }
#endif
}

/**
 * Collect task and resource data for every reachable process id.
 *
 * @param entries Destination array.
 * @param capacity Maximum number of entries to fill.
 * @param countOut Receives the number of valid entries.
 * @return Zero on success, or -1 on invalid arguments.
 */
static long rcman_collect_tasks(RcmanTaskEntry* entries, unsigned long capacity, unsigned long* countOut) {
    unsigned long count = 0UL;

    if ((entries == 0) || (countOut == 0) || (capacity == 0UL)) {
        return -1;
    }

    memset(entries, 0, sizeof(*entries) * capacity);

    for (unsigned long pid = 0UL; (pid < RCMAN_TASK_SCAN_MAX) && (count < capacity); ++pid) {
        long status = getTaskInfo((long)pid, &entries[count].task);

        if (status < 0) {
            continue;
        }

        entries[count].present = 1;
        if (getTaskResourceInfo((long)pid, &entries[count].resource) != 0) {
            memset(&entries[count].resource, 0, sizeof(entries[count].resource));
        }
        ++count;
    }

    *countOut = count;
    return 0;
}

/**
 * Locate one task by process id.
 *
 * @param entries Snapshot array.
 * @param count Entry count.
 * @param pid Target pid.
 * @return Matching index, or -1 when missing.
 */
static int rcman_find_task_index(const RcmanTaskEntry* entries, unsigned long count, long pid) {
    for (unsigned long index = 0UL; index < count; ++index) {
        if ((entries[index].present != 0) && (entries[index].task.id == pid)) {
            return (int)index;
        }
    }

    return -1;
}

/**
 * Build one indented line for the process tree.
 *
 * @param entries Snapshot array.
 * @param count Task count.
 * @param nodeIndex Current node index.
 * @param depth Tree depth used for indentation.
 * @param visited Cycle guard array.
 * @param snapshot Destination line buffer.
 * @return Nothing.
 */
static void rcman_print_node(const RcmanTaskEntry* entries, unsigned long count, int nodeIndex, unsigned long depth, unsigned char* visited, RcmanSnapshot* snapshot) {
    char process_name[64];
    char pid_text[RCMAN_ROW_PID_CAPACITY];
    char parent[RCMAN_ROW_PID_CAPACITY];
    char memory[RCMAN_ROW_MEMORY_CAPACITY];
    char detail[RCMAN_ROW_DETAIL_CAPACITY];
    long process_id;
    long parent_pid;
    const char* state_name;

    if ((entries == 0) || (visited == 0) || (snapshot == 0) || (nodeIndex < 0)) {
        return;
    }
    if (visited[nodeIndex] != 0U) {
        return;
    }

    visited[nodeIndex] = 1U;
    process_id = entries[nodeIndex].task.id;
    parent_pid = entries[nodeIndex].task.parent_process_id;
    state_name = rcman_task_state_name(entries[nodeIndex].task.main_thread_state);
    rcman_format_process_name(&entries[nodeIndex], process_name, sizeof(process_name));
    (void)crt_snprintf(pid_text, sizeof(pid_text), "%ld", process_id);
    (void)crt_snprintf(parent, sizeof(parent), "%ld", parent_pid);
    rcman_format_kib_cell(memory, sizeof(memory), rcman_kib(entries[nodeIndex].resource.total_bytes));
    (void)crt_snprintf(
        detail,
        sizeof(detail),
        "img=%lu KB stack=%lu KB heap=%lu KB raw-state=%ld",
        rcman_kib(entries[nodeIndex].resource.image_bytes),
        rcman_kib(entries[nodeIndex].resource.stack_bytes),
        rcman_kib(entries[nodeIndex].resource.heap_bytes),
        entries[nodeIndex].task.main_thread_state);
    rcman_add_display_row(snapshot, RCMAN_ROW_KIND_PROCESS, depth, process_name, pid_text, parent, state_name, memory, detail);

    rcman_add_process_image_rows(snapshot, &entries[nodeIndex], depth);
    rcman_add_module_rows(snapshot, &entries[nodeIndex], depth);

    for (unsigned long child = 0UL; child < count; ++child) {
        if ((entries[child].present == 0) || (visited[child] != 0U)) {
            continue;
        }
        if (entries[child].task.parent_process_id == process_id) {
            rcman_print_node(entries, count, (int)child, depth + 1UL, visited, snapshot);
        }
    }
}

/**
 * Build a process tree snapshot suitable for both the GUI and console.
 *
 * @param snapshot Source snapshot.
 * @param out_snapshot Destination line buffer.
 * @return Nothing.
 */
static void rcman_build_tree(const RcmanSnapshot* snapshot, RcmanSnapshot* out_snapshot) {
    unsigned char visited[RCMAN_TASK_SCAN_MAX] = { 0 };

    if ((snapshot == 0) || (out_snapshot == 0)) {
        return;
    }

    for (unsigned long index = 0UL; index < snapshot->task_count; ++index) {
        if (snapshot->tasks[index].present == 0) {
            continue;
        }
        if ((snapshot->tasks[index].task.parent_process_id != 0) &&
            (rcman_find_task_index(snapshot->tasks, snapshot->task_count, snapshot->tasks[index].task.parent_process_id) >= 0)) {
            continue;
        }

        rcman_print_node(snapshot->tasks, snapshot->task_count, (int)index, 0UL, visited, out_snapshot);
    }

    for (unsigned long index = 0UL; index < snapshot->task_count; ++index) {
        if ((snapshot->tasks[index].present != 0) && (visited[index] == 0U)) {
            rcman_print_node(snapshot->tasks, snapshot->task_count, (int)index, 0UL, visited, out_snapshot);
        }
    }
}

/**
 * Refresh the cached memory and task snapshot.
 *
 * @param state Window state owning the snapshot.
 * @return Nothing.
 */
static void rcman_refresh_snapshot(RcmanWindowState* state) {
    RcmanSnapshot* snapshot;

    if (state == 0) {
        return;
    }

    snapshot = &state->snapshot;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->refreshed_ms = getUptimeMs();
    snapshot->memory_status = getMemoryInfo(&snapshot->memory);
    if (snapshot->memory_status == 0) {
        snapshot->kernel_used_bytes = (snapshot->memory.total_bytes > snapshot->memory.free_bytes)
            ? (snapshot->memory.total_bytes - snapshot->memory.free_bytes)
            : 0UL;
    }

    snapshot->task_status = rcman_collect_tasks(snapshot->tasks, COUNT_OF(snapshot->tasks), &snapshot->task_count);
    rcman_copy_text(snapshot->header_line, sizeof(snapshot->header_line), "Resource status");
    rcman_copy_text(snapshot->subtitle_line, sizeof(snapshot->subtitle_line), "Refresh interval: 500 ms | scroll: j/k, u/d, g/G, space");

    if (snapshot->memory_status != 0) {
        rcman_copy_text(snapshot->status_line, sizeof(snapshot->status_line), "Memory snapshot unavailable");
    }
    else if (snapshot->task_status != 0) {
        rcman_copy_text(snapshot->status_line, sizeof(snapshot->status_line), "Task snapshot unavailable");
    }
    else {
        rcman_copy_text(snapshot->status_line, sizeof(snapshot->status_line), "CPU counter unavailable; uptime is exposed instead");
    }

    if ((snapshot->memory_status == 0) && (snapshot->task_status == 0)) {
        rcman_build_tree(snapshot, snapshot);
    }
    else {
        rcman_add_display_row(snapshot, RCMAN_ROW_KIND_NOTE, 0UL, "snapshot", "", "", "error", "", "refresh failed");
    }

    rcman_clamp_scroll_row(state, rcman_visible_row_capacity(state));

#if RCMAN_ENABLE_CONSOLE_DEBUG
    {
        const unsigned long console_hash = rcman_snapshot_console_hash(snapshot);

        if ((state->console_snapshot_ready == 0)
            || (state->last_console_dump_hash != console_hash)) {
            state->console_snapshot_ready = 1;
            state->last_console_dump_hash = console_hash;
            rcman_dump_console_snapshot(snapshot);
        }
    }
#endif
}

/**
 * Load the fonts used by the window renderer.
 *
 * @param state Window state.
 * @return Zero on success, or a negative status code on failure.
 */
static long rcman_prepare_fonts(RcmanWindowState* state) {
    long status;

    if (state == 0) {
        return -1L;
    }

    status = GdiLoadFont(0, 23UL, &state->title_font);
    if (status < 0L) {
        return status;
    }
    state->title_font_ready = 1;

    status = GdiLoadFont(0, 14UL, &state->body_font);
    if (status < 0L) {
        GdiUnloadFont(&state->title_font);
        state->title_font_ready = 0;
        return status;
    }
    state->body_font_ready = 1;
    return 0L;
}

/**
 * Release the fonts owned by the window.
 *
 * @param state Window state.
 * @return Nothing.
 */
static void rcman_release_fonts(RcmanWindowState* state) {
    if (state == 0) {
        return;
    }

    if (state->body_font_ready) {
        (void)GdiUnloadFont(&state->body_font);
        state->body_font_ready = 0;
    }
    if (state->title_font_ready) {
        (void)GdiUnloadFont(&state->title_font);
        state->title_font_ready = 0;
    }
}

/**
 * Draw one metric card into the dashboard header.
 *
 * @param surface Mapped window surface.
 * @param font Body font.
 * @param x Card X coordinate.
 * @param y Card Y coordinate.
 * @param width Card width.
 * @param height Card height.
 * @param label Small card label.
 * @param value Main card value.
 * @param accent Accent strip color.
 * @return Nothing.
 */
static void rcman_draw_card(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, unsigned long width, unsigned long height, const char* label, const char* value, unsigned long accent) {
    if ((surface == 0) || (font == 0) || (label == 0) || (value == 0) || (width == 0UL) || (height == 0UL)) {
        return;
    }

    (void)GdiFillSurfaceRect(surface, x, y, width, height, RCMAN_PANEL_COLOR);
    (void)GdiFillSurfaceRect(surface, x, y, width, 4UL, accent);
    (void)GdiDrawTextSurface(surface, font, x + 12UL, y + 10UL, label, RCMAN_MUTED_COLOR);
    (void)GdiDrawTextSurface(surface, font, x + 12UL, y + 30UL, value, accent);
}

/**
 * Paint the rcman dashboard.
 *
 * @param hwnd Target window.
 * @return Zero on success, or a negative status code on failure.
 */
static long rcman_paint_window(HWND hwnd) {
    RosGdiSurface surface;
    unsigned long margin = 18UL;
    unsigned long header_height = 90UL;
    unsigned long card_height = 72UL;
    unsigned long card_gap = 10UL;
    unsigned long card_width;
    unsigned long card_y = 114UL;
    unsigned long panel_y = RCMAN_PANEL_Y;
    unsigned long panel_x = margin;
    unsigned long panel_width;
    unsigned long panel_height;
    unsigned long line_height;
    unsigned long row_height;
    unsigned long visible_rows;
    unsigned long body_top;
    unsigned long body_bottom;
    unsigned long index;
    unsigned long start_row;
    unsigned long end_row;
    unsigned long inner_left;
    unsigned long inner_width;
    unsigned long type_x;
    unsigned long name_x;
    unsigned long pid_x;
    unsigned long pid_width;
    unsigned long ppid_x;
    unsigned long ppid_width;
    unsigned long state_x;
    unsigned long state_width;
    unsigned long memory_x;
    unsigned long memory_width;
    unsigned long detail_x;
    unsigned long footer_y;
    long status;
    long release_status;
    char value[96];
    char range_text[96];
    const RcmanSnapshot* snapshot = &g_rcman.snapshot;

    if ((hwnd == 0UL) || !g_rcman.body_font_ready || !g_rcman.title_font_ready) {
        return -1L;
    }

    status = GdiGetWindowSurface(hwnd, &surface);
    if (status < 0L) {
        return status;
    }

    g_rcman.window_width = surface.width;
    g_rcman.window_height = surface.height;

    (void)GdiFillSurfaceRect(&surface, 0UL, 0UL, surface.width, surface.height, RCMAN_BG_COLOR);

    (void)GdiFillSurfaceRect(&surface, margin, margin, surface.width > (margin * 2UL) ? (surface.width - (margin * 2UL)) : surface.width, header_height, RCMAN_HEADER_COLOR);
    (void)GdiFillSurfaceRect(&surface, margin, margin, 8UL, header_height, RCMAN_GREEN_COLOR);
    (void)GdiDrawTextSurface(&surface, &g_rcman.title_font, margin + 20UL, margin + 14UL, snapshot->header_line, RCMAN_TEXT_COLOR);
    (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, margin + 22UL, margin + 50UL, snapshot->subtitle_line, RCMAN_MUTED_COLOR);
    (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, surface.width > 270UL ? (surface.width - 270UL) : margin + 20UL, margin + 50UL, snapshot->status_line, RCMAN_AMBER_COLOR);

    if (surface.width > (margin * 2UL)) {
        card_width = (surface.width - (margin * 2UL) - (card_gap * 3UL)) / 4UL;
    }
    else {
        card_width = surface.width / 4UL;
    }
    if (card_width == 0UL) {
        card_width = 1UL;
    }

    (void)crt_snprintf(value, sizeof(value), "%lu KB", rcman_kib(snapshot->memory.total_bytes));
    rcman_draw_card(&surface, &g_rcman.body_font, margin, card_y, card_width, card_height, "Max memory", value, RCMAN_BLUE_COLOR);

    (void)crt_snprintf(value, sizeof(value), "%lu KB", rcman_kib(snapshot->memory.free_bytes));
    rcman_draw_card(&surface, &g_rcman.body_font, margin + card_width + card_gap, card_y, card_width, card_height, "Available", value, RCMAN_GREEN_COLOR);

    (void)crt_snprintf(value, sizeof(value), "%lu KB", rcman_kib(snapshot->kernel_used_bytes));
    rcman_draw_card(&surface, &g_rcman.body_font, margin + ((card_width + card_gap) * 2UL), card_y, card_width, card_height, "Kernel heap used", value, RCMAN_AMBER_COLOR);

    (void)crt_snprintf(value, sizeof(value), "%lu", snapshot->task_count);
    rcman_draw_card(&surface, &g_rcman.body_font, margin + ((card_width + card_gap) * 3UL), card_y, card_width, card_height, "Processes", value, RCMAN_RED_COLOR);

    panel_height = surface.height > (panel_y + 24UL) ? (surface.height - panel_y - 20UL) : 0UL;
    panel_width = surface.width > (margin * 2UL) ? (surface.width - (margin * 2UL)) : surface.width;
    if (panel_height > 0UL) {
        (void)GdiFillSurfaceRect(&surface, panel_x, panel_y, panel_width, panel_height, RCMAN_PANEL_ALT_COLOR);
        (void)GdiFillSurfaceRect(&surface, panel_x, panel_y, panel_width, 4UL, RCMAN_BLUE_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, panel_x + 12UL, panel_y + 12UL, "Processes and modules", RCMAN_TEXT_COLOR);
    }

    line_height = g_rcman.body_font.line_height != 0UL ? g_rcman.body_font.line_height : 18UL;
    row_height = line_height + RCMAN_TABLE_ROW_GAP;
    body_top = panel_y + RCMAN_TABLE_HEADER_HEIGHT + 18UL;
    body_bottom = surface.height > (RCMAN_TABLE_FOOTER_HEIGHT + 18UL) ? (surface.height - RCMAN_TABLE_FOOTER_HEIGHT - 18UL) : surface.height;
    visible_rows = (body_bottom > body_top) ? ((body_bottom - body_top) / row_height) : 0UL;
    rcman_clamp_scroll_row(&g_rcman, visible_rows);
    start_row = g_rcman.scroll_row;
    end_row = start_row + visible_rows;
    if (end_row > snapshot->row_count) {
        end_row = snapshot->row_count;
    }

    inner_left = panel_x + 12UL;
    inner_width = panel_width > 36UL ? (panel_width - 36UL) : panel_width;
    type_x = inner_left;
    name_x = inner_left + 46UL;
    pid_x = name_x + 176UL;
    pid_width = 46UL;
    ppid_x = pid_x + pid_width;
    ppid_width = 50UL;
    state_x = ppid_x + ppid_width;
    state_width = 64UL;
    memory_x = state_x + state_width;
    memory_width = 88UL;
    detail_x = memory_x + memory_width;

    if (panel_height > 0UL) {
        unsigned long header_y = panel_y + 34UL;

        (void)GdiFillSurfaceRect(&surface, panel_x + 8UL, header_y - 4UL, panel_width > 16UL ? (panel_width - 16UL) : panel_width, row_height, RCMAN_HEADER_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, type_x, header_y, "Type", RCMAN_MUTED_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, name_x, header_y, "Name", RCMAN_MUTED_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, pid_x, header_y, "PID", RCMAN_MUTED_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, ppid_x, header_y, "PPID", RCMAN_MUTED_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, state_x, header_y, "State", RCMAN_MUTED_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, memory_x, header_y, "Memory", RCMAN_MUTED_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, detail_x, header_y, "Details", RCMAN_MUTED_COLOR);
    }

    for (index = start_row; index < end_row; ++index) {
        const RcmanDisplayRow* row = &snapshot->rows[index];
        unsigned long screen_row = index - start_row;
        unsigned long row_y = body_top + (screen_row * row_height);
        unsigned long accent_color = rcman_row_accent_color(row);
        unsigned long name_color = ((RcmanRowKind)row->kind == RCMAN_ROW_KIND_PATH) ? RCMAN_MUTED_COLOR : RCMAN_TEXT_COLOR;
        unsigned long state_color = ((RcmanRowKind)row->kind == RCMAN_ROW_KIND_PROCESS) ? rcman_state_color(row->state) : accent_color;
        unsigned long name_indent = row->depth * 14UL;
        char row_type[8];
        char name_text[RCMAN_ROW_NAME_CAPACITY];
        char detail_text[RCMAN_ROW_DETAIL_CAPACITY];

        switch ((RcmanRowKind)row->kind) {
        case RCMAN_ROW_KIND_PROCESS:
            rcman_copy_text(row_type, sizeof(row_type), "PROC");
            break;
        case RCMAN_ROW_KIND_MODULE:
            rcman_copy_text(row_type, sizeof(row_type), "DLL");
            break;
        case RCMAN_ROW_KIND_PATH:
            rcman_copy_text(row_type, sizeof(row_type), "PATH");
            break;
        default:
            rcman_copy_text(row_type, sizeof(row_type), "NOTE");
            break;
        }

        rcman_copy_text_limited(name_text, sizeof(name_text), row->name, RCMAN_UI_NAME_MAX_CHARS);
        rcman_copy_text_limited(
            detail_text,
            sizeof(detail_text),
            row->detail,
            ((RcmanRowKind)row->kind == RCMAN_ROW_KIND_PATH) ? RCMAN_UI_PATH_DETAIL_MAX_CHARS : RCMAN_UI_DETAIL_MAX_CHARS);

        (void)GdiFillSurfaceRect(&surface, panel_x + 8UL, row_y - 2UL, panel_width > 16UL ? (panel_width - 16UL) : panel_width, row_height, rcman_row_background_color(index, row));
        (void)GdiFillSurfaceRect(&surface, panel_x + 8UL, row_y - 2UL, 4UL, row_height, accent_color);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, type_x, row_y, row_type, accent_color);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, name_x + name_indent, row_y, name_text, name_color);
        if (row->pid[0] != '\0') {
            (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, pid_x, row_y, row->pid, RCMAN_TEXT_COLOR);
        }
        if (row->parent[0] != '\0') {
            (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, ppid_x, row_y, row->parent, RCMAN_MUTED_COLOR);
        }
        if (row->state[0] != '\0') {
            (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, state_x, row_y, row->state, state_color);
        }
        if (row->memory[0] != '\0') {
            (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, memory_x, row_y, row->memory, RCMAN_AMBER_COLOR);
        }
        if (row->detail[0] != '\0') {
            (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, detail_x, row_y, detail_text, ((RcmanRowKind)row->kind == RCMAN_ROW_KIND_PATH) ? RCMAN_MUTED_COLOR : RCMAN_TEXT_COLOR);
        }
    }

    footer_y = surface.height > (RCMAN_TABLE_FOOTER_HEIGHT + 10UL) ? (surface.height - RCMAN_TABLE_FOOTER_HEIGHT) : surface.height;
    if (panel_height > 0UL) {
        unsigned long first_visible = (snapshot->row_count == 0UL) ? 0UL : (start_row + 1UL);
        unsigned long last_visible = end_row;

        (void)crt_snprintf(range_text, sizeof(range_text), "rows %lu-%lu / %lu", first_visible, last_visible, snapshot->row_count);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, panel_x + 12UL, footer_y, "keys: j/k step  u/d page  g/G ends  space page-down", RCMAN_MUTED_COLOR);
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, surface.width > 180UL ? (surface.width - 180UL) : (panel_x + 12UL), footer_y, range_text, RCMAN_MUTED_COLOR);
    }

    if ((snapshot->row_count > visible_rows) && (panel_height > 0UL) && (visible_rows > 0UL)) {
        unsigned long track_x = panel_x + panel_width - 10UL;
        unsigned long track_y = body_top;
        unsigned long track_height = body_bottom > body_top ? (body_bottom - body_top) : 0UL;
        unsigned long thumb_height = (snapshot->row_count == 0UL) ? track_height : ((visible_rows * track_height) / snapshot->row_count);
        unsigned long max_scroll = rcman_max_scroll_row(snapshot->row_count, visible_rows);
        unsigned long thumb_y = track_y;

        if (thumb_height < 18UL) {
            thumb_height = 18UL;
        }
        if (thumb_height > track_height) {
            thumb_height = track_height;
        }
        if ((max_scroll > 0UL) && (track_height > thumb_height)) {
            thumb_y = track_y + ((g_rcman.scroll_row * (track_height - thumb_height)) / max_scroll);
        }

        (void)GdiFillSurfaceRect(&surface, track_x, track_y, 4UL, track_height, RCMAN_HEADER_COLOR);
        (void)GdiFillSurfaceRect(&surface, track_x, thumb_y, 4UL, thumb_height, RCMAN_BLUE_COLOR);
    }

    if ((snapshot->row_count == 0UL) && (panel_height > 0UL)) {
        (void)GdiDrawTextSurface(&surface, &g_rcman.body_font, panel_x + 12UL, body_top, "No process rows available", RCMAN_MUTED_COLOR);
    }

    /*
     * Direct surface writes do not reach the screen until GWES sees an
     * explicit dirty region for the client window. rcman redraws the whole
     * surface on each refresh, so invalidate the full client area before the
     * release hands control back to the compositor.
     */
    status = GdiInvalidateRect(hwnd, 0UL, 0UL, surface.width, surface.height);
    release_status = GdiReleaseWindowSurface(hwnd);
    if ((release_status < 0L) && (status >= 0L)) {
        status = release_status;
    }
    return status;
}

/**
 * Request a full repaint using the last known client size.
 *
 * @param hwnd Target window.
 * @return Nothing.
 */
static void rcman_request_repaint(HWND hwnd) {
    if ((hwnd == 0UL) || (g_rcman.window_width == 0UL) || (g_rcman.window_height == 0UL)) {
        return;
    }

    (void)GdiInvalidateRect(hwnd, 0UL, 0UL, g_rcman.window_width, g_rcman.window_height);
}

/**
 * Handle rcman window messages.
 *
 * @param hwnd Target window.
 * @param message Message id.
 * @param wParam Message word 0.
 * @param lParam Message word 1.
 * @return Zero for handled messages.
 */
static LRESULT rcman_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    if (message == WM_CREATE) {
        g_rcman.hwnd = hwnd;
        g_rcman.window_width = RCMAN_WINDOW_WIDTH;
        g_rcman.window_height = RCMAN_WINDOW_HEIGHT;

        if (rcman_prepare_fonts(&g_rcman) < 0L) {
            writeLog("rcman.exe: font initialization failed");
            (void)PostQuitMessage(1L);
            return 0L;
        }

        memset(&g_rcman.snapshot, 0, sizeof(g_rcman.snapshot));
        g_rcman.scroll_row = 0UL;
        g_rcman.snapshot.refreshed_ms = getUptimeMs();
        rcman_copy_text(g_rcman.snapshot.header_line, sizeof(g_rcman.snapshot.header_line), "Resource status");
        rcman_copy_text(g_rcman.snapshot.subtitle_line, sizeof(g_rcman.snapshot.subtitle_line), "Refresh interval: 500 ms | scroll: j/k, u/d, g/G, space");
        rcman_copy_text(g_rcman.snapshot.status_line, sizeof(g_rcman.snapshot.status_line), "Collecting initial snapshot...");
        g_rcman.timer_id = SetTimer(hwnd, 1UL, RCMAN_REFRESH_MSEC);
        if (g_rcman.timer_id == 0UL) {
            writeLog("rcman.exe: timer setup failed");
            rcman_refresh_snapshot(&g_rcman);
        }

        if (rcman_paint_window(hwnd) < 0L) {
            rcman_request_repaint(hwnd);
        }
        return 0L;
    }

    if (message == WM_SIZE) {
        long width = 0L;
        long height = 0L;

        WindowUnpackSignedPair(lParam, &width, &height);
        g_rcman.window_width = width > 0L ? (unsigned long)width : g_rcman.window_width;
        g_rcman.window_height = height > 0L ? (unsigned long)height : g_rcman.window_height;
        if (rcman_paint_window(hwnd) < 0L) {
            rcman_request_repaint(hwnd);
        }
        return 0L;
    }

    if (message == WM_TIMER) {
        if ((g_rcman.timer_id == 0UL) || (wParam == g_rcman.timer_id)) {
            rcman_refresh_snapshot(&g_rcman);
            if (rcman_paint_window(hwnd) < 0L) {
                rcman_request_repaint(hwnd);
            }
        }
        return 0L;
    }

    if ((message == WM_PAINT) || (message == WM_REPAINT)) {
        (void)rcman_paint_window(hwnd);
        return 0L;
    }

    if (message == WM_KEYDOWN) {
        unsigned long visible_rows = rcman_visible_row_capacity(&g_rcman);
        long page_rows = (visible_rows > 1UL) ? (long)(visible_rows - 1UL) : 5L;
        int changed = 0;

        switch (wParam) {
        case 'k':
        case 'K':
        case 'w':
        case 'W':
            changed = rcman_scroll_rows(&g_rcman, -1L, visible_rows);
            break;
        case 'j':
        case 'J':
        case 's':
        case 'S':
            changed = rcman_scroll_rows(&g_rcman, 1L, visible_rows);
            break;
        case 'u':
        case 'U':
            changed = rcman_scroll_rows(&g_rcman, -page_rows, visible_rows);
            break;
        case 'd':
        case 'D':
        case ' ':
            changed = rcman_scroll_rows(&g_rcman, page_rows, visible_rows);
            break;
        case 'g':
            changed = (g_rcman.scroll_row != 0UL);
            g_rcman.scroll_row = 0UL;
            break;
        case 'G':
        {
            unsigned long bottom = rcman_max_scroll_row(g_rcman.snapshot.row_count, visible_rows);

            changed = (g_rcman.scroll_row != bottom);
            g_rcman.scroll_row = bottom;
        }
        break;
        case 'r':
        case 'R':
            rcman_refresh_snapshot(&g_rcman);
            changed = 1;
            break;
        default:
            break;
        }

        if (changed) {
            if (rcman_paint_window(hwnd) < 0L) {
                rcman_request_repaint(hwnd);
            }
        }
        return 0L;
    }

    if ((message == WM_CLOSE) || (message == WM_DESTROY)) {
        if (g_rcman.timer_id != 0UL) {
            (void)KillTimer(hwnd, g_rcman.timer_id);
            g_rcman.timer_id = 0UL;
        }
        rcman_release_fonts(&g_rcman);
        (void)PostQuitMessage(0L);
        return 0L;
    }

    return 0L;
}

/**
 * Dump the cached snapshot to the console when GUI startup fails.
 *
 * @param snapshot Snapshot to print.
 * @return Nothing.
 */
static void rcman_dump_console_snapshot(const RcmanSnapshot* snapshot) {
    unsigned long index;
    char header_line[RCMAN_LINE_CAPACITY];

    if (snapshot == 0) {
        return;
    }

    Console::WriteLine(Console::Colors::BrightCyan, "rcman.exe: resource status refreshed=%lu ms", snapshot->refreshed_ms);
    Console::WriteLine(Console::Colors::BrightYellow, "  status: %s", snapshot->status_line);
    Console::WriteLine(Console::Colors::BrightBlue, "  max memory: %lu KB", rcman_kib(snapshot->memory.total_bytes));
    Console::WriteLine(Console::Colors::BrightGreen, "  available memory: %lu KB", rcman_kib(snapshot->memory.free_bytes));
    Console::WriteLine(Console::Colors::BrightYellow, "  kernel heap used: %lu KB", rcman_kib(snapshot->kernel_used_bytes));
    Console::WriteLine(Console::Colors::BrightBlack, "  cpu counter: unavailable (uptime is exposed, not CPU usage)");
    Console::WriteLine(Console::Colors::BrightRed, "  process count: %lu", snapshot->task_count);
    rcman_format_console_columns(header_line, sizeof(header_line), "Type", "Name", "PID", "PPID", "State", "Memory", "Details");
    Console::WriteLine(Console::Colors::BrightBlack, "%s", header_line);
    for (index = 0UL; index < snapshot->row_count; ++index) {
        const RcmanDisplayRow* row = &snapshot->rows[index];
        Console::Colors color = Console::Colors::White;

        switch ((RcmanRowKind)row->kind) {
        case RCMAN_ROW_KIND_PROCESS:
            if (0 == crt_strcmp(row->state, "run")) {
                color = Console::Colors::BrightGreen;
            }
            else if (0 == crt_strcmp(row->state, "wait")) {
                color = Console::Colors::BrightYellow;
            }
            else if (0 == crt_strcmp(row->state, "dead")) {
                color = Console::Colors::BrightRed;
            }
            else {
                color = Console::Colors::BrightWhite;
            }
            break;
        case RCMAN_ROW_KIND_MODULE:
            color = Console::Colors::BrightBlue;
            break;
        case RCMAN_ROW_KIND_PATH:
            color = Console::Colors::BrightBlack;
            break;
        default:
            color = Console::Colors::BrightYellow;
            break;
        }

        Console::WriteLine(color, "%s", snapshot->tree_lines[index]);
    }
    Console::WriteLine(Console::Colors::BrightCyan, "rcman.exe: end snapshot");
}

/**
 * Start rcman as a GUI dashboard.
 *
 * @return Process exit code.
 */
int main(void) {
    MSG message;
    WindowCreateParams params;

    if (CreateWindowClass(RCMAN_WINDOW_CLASS, rcman_wndproc) < 0L) {
        writeLog("rcman.exe: CreateWindowClass failed");
        rcman_refresh_snapshot(&g_rcman);
        rcman_dump_console_snapshot(&g_rcman.snapshot);
        return 1;
    }

    params.class_name = RCMAN_WINDOW_CLASS;
    params.title = RCMAN_WINDOW_TITLE;
    params.parent = 0UL;
    params.x = RCMAN_WINDOW_X;
    params.y = RCMAN_WINDOW_Y;
    params.width = RCMAN_WINDOW_WIDTH;
    params.height = RCMAN_WINDOW_HEIGHT;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_DECORATED | ROS_WINDOW_STYLE_BORDER;

    g_rcman.hwnd = CreateWindowEx(&params);
    if (g_rcman.hwnd == 0UL) {
        writeLog("rcman.exe: CreateWindowEx failed");
        rcman_refresh_snapshot(&g_rcman);
        rcman_dump_console_snapshot(&g_rcman.snapshot);
        return 2;
    }

    for (;;) {
        long get_result = GetMessage(&message);

        if (get_result <= 0L) {
            break;
        }

        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    if (g_rcman.timer_id != 0UL) {
        (void)KillTimer(g_rcman.hwnd, g_rcman.timer_id);
        g_rcman.timer_id = 0UL;
    }

    rcman_release_fonts(&g_rcman);
    return 0;
}