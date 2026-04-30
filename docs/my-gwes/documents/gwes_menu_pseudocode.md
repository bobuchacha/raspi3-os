# my-gwes Popup Menu Pseudocode

This note provides pseudocode for the first implementation pass of a CE-style popup menu in `my-gwes`.

## 1. Data structures

```text
struct GWE_MENU_ITEM {
    id: uint32
    flags: uint32
    text: string
    submenu: GWE_MENU*
    ownerData: uintptr
    bounds: rect
}

struct GWE_MENU {
    context: GWE_CONTEXT*
    items: array<GWE_MENU_ITEM>
    selectedIndex: int
    popupWindow: GWE_WINDOW*
    parentMenu: GWE_MENU*
    parentItemIndex: int
    childMenu: GWE_MENU*
    ownerWindow: GWE_WINDOW*
    anchorX: int
    anchorY: int
    flags: uint32
    isTracking: bool
    isSubmenuOpen: bool
    returnCommandMode: bool
    pendingCommandId: uint32
    pendingReplay: bool
    replayPoint: point
    replayButtons: uint32
    nonDismissRect: rect
}
```

## 2. Root API

```text
function gwe_menu_track_popup(context, menu, ownerWindow, x, y, flags, outCommandId):
    if context is null or menu is null or ownerWindow is null:
        return INVALID_ARG

    if another root menu is active:
        dismiss active root menu

    menu.ownerWindow = ownerWindow
    menu.anchorX = x
    menu.anchorY = y
    menu.returnCommandMode = has_flag(flags, RETURN_COMMAND)
    menu.pendingCommandId = 0
    menu.selectedIndex = first_selectable_item(menu)

    result = prepare_menu_popup(menu)
    if result != OK:
        return result

    result = show_menu_popup(menu)
    if result != OK:
        destroy_menu_popup(menu)
        return result

    set_capture(context, menu.popupWindow)
    menu.isTracking = true
    notify_owner_menu_enter(ownerWindow, menu)

    while menu.isTracking:
        msg = get_message_for_owner_thread(context, ownerWindow.ownerTid)
        if msg is empty:
            continue

        if msg is quit:
            dismiss_menu_tree(menu, DISMISS_QUIT)
            break

        dispatch_message(context, msg)

    notify_owner_menu_exit(ownerWindow, menu)

    if outCommandId is not null:
        *outCommandId = menu.pendingCommandId

    return OK
```

## 3. Popup preparation

```text
function prepare_menu_popup(menu):
    measure_each_item(menu)
    compute_menu_size(menu)
    place_root_popup(menu)

    createInfo.ownerTid = menu.ownerWindow.ownerTid
    createInfo.parent = null
    createInfo.rect = { x: menu.anchorX, y: menu.anchorY, w: menu.width, h: menu.height }
    createInfo.flags = VISIBLE | ENABLED | TOPLEVEL | MODAL
    createInfo.proc = gwe_menu_window_proc
    createInfo.userData = menu
    createInfo.name = "PopupMenu"

    return gwe_create_window(menu.context, createInfo, &menu.popupWindow)
```

## 4. Menu window proc

```text
function gwe_menu_window_proc(context, window, message, userData):
    menu = cast userData to GWE_MENU*

    switch message.type:
    case CREATE:
        return 0

    case PAINT:
        paint_popup_background(menu)
        for each item in menu.items:
            draw_menu_item(menu, item, item is selected)
        return 0

    case POINTER_MOVE:
        return handle_menu_pointer_move(menu, message.screenPoint)

    case POINTER_DOWN:
        return handle_menu_pointer_down(menu, message.screenPoint, message.wparam)

    case POINTER_UP:
        return handle_menu_pointer_up(menu, message.screenPoint, message.wparam)

    case KEY_DOWN:
        return handle_menu_key_down(menu, message.wparam)

    case DESTROY:
        menu.popupWindow = null
        return 0

    default:
        return 0
```

## 5. Pointer move

```text
function handle_menu_pointer_move(menu, screenPoint):
    if point_inside_menu(menu, screenPoint):
        index = item_index_from_point(menu, screenPoint)
        if index != menu.selectedIndex and item_can_highlight(menu.items[index]):
            menu.selectedIndex = index
            invalidate_menu_item_range(menu)
            notify_owner_highlight_changed(menu)

            if item_has_submenu(menu.items[index]):
                arm_submenu_open_timer(menu, index)
            else:
                close_child_submenu_if_open(menu)
        return 0

    if child_menu_allows_pointer(menu, screenPoint):
        return 0

    dismiss_menu_tree(menu, DISMISS_OUTSIDE_MOVE)
    return 0
```

## 6. Pointer down and outside replay

```text
function handle_menu_pointer_down(menu, screenPoint, buttons):
    if point_inside_menu(menu, screenPoint):
        index = item_index_from_point(menu, screenPoint)
        if index is valid and item_can_highlight(menu.items[index]):
            menu.selectedIndex = index
        return 0

    menu.pendingReplay = true
    menu.replayPoint = screenPoint
    menu.replayButtons = buttons
    dismiss_menu_tree(menu, DISMISS_OUTSIDE_CLICK)
    return 0
```

```text
function replay_pending_click_if_needed(rootMenu):
    if not rootMenu.pendingReplay:
        return

    target = hit_test_topmost_excluding_menu_windows(rootMenu.context, rootMenu.replayPoint)
    if target is null:
        return

    synthesize_pointer_down(target, rootMenu.replayPoint, rootMenu.replayButtons)
```

## 7. Pointer up and command execution

```text
function handle_menu_pointer_up(menu, screenPoint, buttons):
    if not point_inside_menu(menu, screenPoint):
        dismiss_menu_tree(menu, DISMISS_OUTSIDE_CLICK)
        return 0

    index = item_index_from_point(menu, screenPoint)
    if index is invalid:
        return 0

    item = menu.items[index]

    if item is separator or item is disabled:
        return 0

    if item has submenu:
        open_child_submenu(menu, index)
        return 0

    execute_menu_command(menu, item.id)
    return 0
```

```text
function execute_menu_command(menu, commandId):
    root = root_menu(menu)
    root.pendingCommandId = commandId

    if root.returnCommandMode:
        dismiss_menu_tree(root, DISMISS_EXECUTE)
        return

    send_command_notification(root.ownerWindow, commandId)
    dismiss_menu_tree(root, DISMISS_EXECUTE)
```

## 8. Keyboard navigation

```text
function handle_menu_key_down(menu, keyCode):
    switch keyCode:
    case KEY_ESCAPE:
        dismiss_menu_tree(root_menu(menu), DISMISS_ESCAPE)
        return 0

    case KEY_UP:
        menu.selectedIndex = previous_selectable_item(menu, menu.selectedIndex)
        invalidate_menu(menu)
        return 0

    case KEY_DOWN:
        menu.selectedIndex = next_selectable_item(menu, menu.selectedIndex)
        invalidate_menu(menu)
        return 0

    case KEY_RIGHT:
        if selected_item_has_submenu(menu):
            open_child_submenu(menu, menu.selectedIndex)
        return 0

    case KEY_LEFT:
        if menu.parentMenu exists:
            dismiss_submenu_only(menu)
        return 0

    case KEY_ENTER:
        if current item is executable:
            execute_menu_command(menu, current item id)
        return 0

    default:
        try mnemonic match
        return 0
```

## 9. Submenu opening

```text
function open_child_submenu(menu, itemIndex):
    item = menu.items[itemIndex]
    if item.submenu is null:
        return

    close_child_submenu_if_open(menu)

    child = item.submenu
    child.parentMenu = menu
    child.parentItemIndex = itemIndex
    child.ownerWindow = root_menu(menu).ownerWindow
    child.anchorX = compute_submenu_x(menu, itemIndex)
    child.anchorY = compute_submenu_y(menu, itemIndex)

    prepare_menu_popup(child)
    show_menu_popup(child)
    menu.childMenu = child
    root_menu(menu).nonDismissRect = union_of_parent_and_child_corridor(menu, child)
```

## 10. Dismissal

```text
function dismiss_menu_tree(menu, reason):
    root = root_menu(menu)
    dismiss_menu_branch_recursively(root)
    release_capture(root.context, root.popupWindow)
    root.isTracking = false
    destroy_menu_popup_windows(root)
    replay_pending_click_if_needed(root)
```

## 11. Paint

```text
function draw_menu_item(menu, item, isSelected):
    if item is separator:
        draw_horizontal_rule(item.bounds)
        return

    if isSelected:
        fill_highlight(item.bounds)
    else:
        fill_normal_background(item.bounds)

    if item is disabled:
        draw_text_disabled(item.text)
    else:
        draw_text_normal(item.text)

    if item has submenu:
        draw_submenu_arrow(item.bounds)

    if item is checked:
        draw_checkmark(item.bounds)
```

## 12. Minimal first implementation scope

For the first pass, implement only:

- root popup menu
- command items and separators
- pointer selection
- keyboard up, down, enter, escape
- dismissal on outside click
- return-command mode

Delay these until phase two or three:

- submenu timers
- scrolling menus
- owner-draw callbacks
- mnemonic tables
- gesture support
