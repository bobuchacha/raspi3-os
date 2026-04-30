# my-gwes Popup Menu Plan

This note turns the WinCE popup menu analysis into an implementation plan for `my-gwes`.

## Goal

Implement a CE-style popup menu subsystem that behaves like `TrackPopupMenuEx` on top of the current `my-gwes` scaffold.

The design target is behavioral compatibility, not binary or class-name compatibility. The WinCE 8 source in this workspace exposes the public API boundary and some shell hooks, but not the private GWES popup window class implementation. Older GWES headers in the workspace show the internal popup menu architecture clearly enough to reproduce the behavior.

## Derived WinCE behavior

- A popup menu is a real top-level window owned by GWES.
- `TrackPopupMenuEx` enters a modal menu loop until the menu is dismissed or a command is chosen.
- The active popup holds input capture while it is visible.
- Pointer motion changes the selected item.
- Pointer-up on an enabled command item executes that item.
- Pointer activity outside the menu dismisses the popup.
- Submenus are modeled as child popup menus connected to a parent menu item.
- Submenu open and close can be timer-driven.
- `TPM_RETURNCMD` returns the chosen item id instead of sending a command notification.
- Owner-draw and separator items are represented in the menu data model, not as ordinary text items.
- The menu engine keeps enough state to replay an outside click after dismissal.

## Why this fits `my-gwes`

The current scaffold already has the core primitives required for a popup menu:

- window creation and destruction
- top-level z-order
- per-thread message queues
- synchronous dispatch
- focus and activation
- capture routing in `gwe_dispatch_pointer`

That means the menu implementation can be added as a higher-level subsystem without reworking the existing core.

## Required extensions

### 1. Menu data model

Add menu-specific structures separate from ordinary windows.

Suggested structures:

- `GWE_MENU`: owns items, runtime state, parent/child popup links, tracking flags
- `GWE_MENU_ITEM`: command id, flags, title, optional submenu pointer, optional owner data
- `GWE_MENU_TRACK`: one active tracking session including owner window, return mode, selected command, dismissal cause, and pending click replay

Suggested item flags:

- command
- separator
- submenu
- disabled
- checked
- default
- owner-draw

### 2. Menu window role

Represent the visible popup as a normal `GWE_WINDOW` with menu-specific user data.

Suggested menu window properties:

- top-level
- visible
- enabled
- modal
- no backing store sharing outside menu code
- explicit geometry computed from item metrics

The menu window should not own the item model. The menu model should outlive the transient popup window used to display it.

### 3. Message contract

Add menu-specific messages on top of `GWE_MSG_USER`.

Suggested messages:

- `GWE_MSG_MENU_INIT`
- `GWE_MSG_MENU_ENTER_LOOP`
- `GWE_MSG_MENU_EXIT_LOOP`
- `GWE_MSG_MENU_HIGHLIGHT_CHANGED`
- `GWE_MSG_MENU_EXECUTE`
- `GWE_MSG_MENU_OPEN_SUBMENU`
- `GWE_MSG_MENU_CLOSE_SUBMENU`
- `GWE_MSG_MENU_TIMER_OPEN`
- `GWE_MSG_MENU_TIMER_CLOSE`
- `GWE_MSG_MENU_TIMER_SCROLL`

This gives you a clean place to model `WM_INITMENUPOPUP`, `WM_MENUSELECT`, and the menu-loop lifecycle without overloading the base pointer and key messages.

### 4. Timer service

Add a lightweight timer facility before implementing submenu hover-open and scrolling menus.

Minimum timer uses:

- delayed submenu open on hover
- delayed submenu close when pointer leaves cascade path
- repeated scrolling while arrow zones are held

### 5. Pending click replay

Current pointer routing sends all pointer messages to the capture window while capture is active. That is necessary, but not sufficient, for a menu.

When the user clicks outside the menu:

1. the menu must dismiss
2. capture must be released
3. the outside click must optionally be replayed to the underlying target window

This replay path is important if you want behavior close to CE and desktop Windows popup menus.

## Implementation phases

### Phase 1. Static popup menu

Build a single popup menu with no submenu and no timers.

Deliverables:

- create and destroy menu model
- append command and separator items
- compute width and height from text length
- create popup menu window at a screen position
- capture pointer while popup is active
- update highlighted item on pointer move
- execute selected command on pointer up
- dismiss on outside click or `Esc`
- return selected command id

### Phase 2. Owner notification modes

Add two execution modes:

- send command-style notification to owner window
- return command id directly to caller

This phase corresponds to the `TPM_RETURNCMD` split.

### Phase 3. Keyboard navigation

Add:

- up and down selection
- enter to execute
- escape to dismiss
- right to open submenu
- left to close submenu
- mnemonic matching by first character initially

### Phase 4. Cascading submenus

Add:

- submenu items linked to another `GWE_MENU`
- child popup window positioned beside parent item
- parent-child dismissal rules
- delayed open and close timers
- cascade-aware hit testing so diagonal movement into submenu does not close immediately

### Phase 5. Scrolling and owner-draw

Add:

- viewport-based menu rendering when the menu exceeds screen height
- up and down scroll zones or arrows
- callback-based measurement and drawing for owner-draw items

## Module split proposal

Suggested files:

- `include/gwe_menu.h`: public menu API and flags
- `src/gwe_menu.c`: menu model lifetime, item management, tracking entry points
- `src/gwe_menu_window.c`: popup menu window proc, hit testing, paint, input handling
- `src/gwe_timer.c`: minimal timer queue support if not introduced elsewhere
- `tests/menu_smoke.c`: command selection, dismissal, replay, and submenu behavior

## Public API proposal

Suggested first-cut API:

```c
GWE_RESULT gwe_menu_create(PGWE_CONTEXT context, PGWE_MENU *outMenu);
GWE_RESULT gwe_menu_destroy(PGWE_CONTEXT context, PGWE_MENU menu);
GWE_RESULT gwe_menu_append_item(PGWE_CONTEXT context, PGWE_MENU menu, const GWE_MENU_ITEM_CREATEINFO *item);
GWE_RESULT gwe_menu_append_separator(PGWE_CONTEXT context, PGWE_MENU menu);
GWE_RESULT gwe_menu_track_popup(PGWE_CONTEXT context,
                                PGWE_MENU menu,
                                PGWE_WINDOW ownerWindow,
                                int32_t screenX,
                                int32_t screenY,
                                uint32_t trackFlags,
                                uint32_t *outCommandId);
```

## Core runtime rules

- Only one root popup tracking session should be active at a time.
- A submenu shares the same logical tracking session as its root popup.
- Root popup dismissal closes the entire cascade.
- Executing a leaf command closes the entire cascade.
- Disabled items can highlight but cannot execute.
- Separators never highlight or execute.
- If capture is lost unexpectedly, dismiss the popup tree safely.

## Geometry rules

- The popup anchor point is in screen coordinates.
- The root popup prefers dropping downward and to the right.
- If the popup would exceed the right or bottom edge, clamp or flip placement.
- A submenu prefers the right side of its parent item and flips left when required.
- Keep one exclusion rectangle so the pointer can move from parent item into submenu without premature dismissal.

## Testing plan

### Functional tests

- create popup and dismiss with outside click
- execute first command with pointer
- execute highlighted command with keyboard enter
- disabled item does not execute
- separator does not highlight
- submenu opens and child command executes
- `Esc` closes cascade without command result
- command id is returned when return mode is enabled

### Edge cases

- owner window destroyed while menu is visible
- capture released by unrelated code
- repeated rapid pointer move across submenu boundary
- empty menu
- menu larger than screen height

## Recommended order for your codebase

1. add menu data structures and menu creation API
2. add a menu popup window proc using the existing window subsystem
3. add root popup tracking with pointer capture and command return
4. add keyboard navigation
5. add submenu cascade logic
6. add timers
7. add owner-draw and scrolling

## Diagram files

- `gwes_menu_flow.mmd`: popup tracking and command flow
- `gwes_menu_state.mmd`: popup session state machine
