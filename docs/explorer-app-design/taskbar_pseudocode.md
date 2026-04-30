# Taskbar Pseudocode

This is pseudocode for a CE-style taskbar designed from the shell/taskbar contract visible in the CE7 sources.

It is intentionally implementation-oriented so you can translate it into your own OS code later.

## 1. Core Data Structures

```text
enum DockEdge
    TOP
    BOTTOM

enum ItemVisualState
    NORMAL
    HOT
    PRESSED
    DISABLED
    ACTIVE

struct Rect
    x
    y
    width
    height

struct TaskItem
    windowId
    titleText
    iconHandle
    isVisible
    isActive
    isFlashing
    visualState
    bounds

struct TrayIcon
    ownerWindowId
    iconId
    callbackMessage
    iconHandle
    tooltipText
    isVisible
    visualState
    bounds

struct NotificationItem
    notificationId
    clsid
    title
    body
    html
    targetWindowId
    dismissable
    isActive

struct TaskbarMetrics
    barThickness
    leftPadding
    rightPadding
    topPadding
    bottomPadding
    elementSpacing
    buttonWidth
    buttonMinWidth
    buttonHeight
    iconSize
    borderThickness

struct TaskbarRects
    barRect
    clientRect
    startButtonRect
    doneButtonRect
    titleRect
    taskButtonsRect
    trayRect
    sipButtonRect
    notificationRect

struct TaskbarState
    hwndTaskbar
    hwndDesktop
    dockEdge
    isVisible
    isFullscreenHidden
    isTopMost
    useSingleTitleMode
    showStartButton
    showDoneButton
    showSipButton
    currentNavText
    foregroundWindowId
    pressedPart
    hotPart
    capturedPart
    tasks : list<TaskItem>
    trayIcons : list<TrayIcon>
    notifications : list<NotificationItem>
    metrics : TaskbarMetrics
    rects : TaskbarRects
    visibleDesktopRect
    fullScreenOwnerWindowId
```

## 2. Initialization

```text
function Taskbar_Create()
    state = zeroed TaskbarState

    state.dockEdge = BOTTOM
    state.isVisible = true
    state.isTopMost = true
    state.showStartButton = true
    state.showDoneButton = false
    state.showSipButton = true
    state.useSingleTitleMode = true

    LoadThemeResources(state)
    LoadMetrics(state)

    hwnd = CreateWindow(
        className = "TaskbarWindow",
        style = WS_VISIBLE | WS_CLIPCHILDREN,
        exStyle = WS_EX_TOPMOST
    )

    state.hwndTaskbar = hwnd

    RegisterTaskBarEx(hwnd, state.dockEdge == TOP)
    Taskbar_UpdateDesktopMetrics(state)
    Taskbar_RecalculateLayout(state)
    Taskbar_ApplyWorkArea(state)
    ShowWindow(hwnd, SHOW)
```

## 3. Desktop And Work Area Management

```text
function Taskbar_UpdateDesktopMetrics(state)
    screenRect = GetPrimaryDisplayRect()

    if state.isVisible == false or state.isFullscreenHidden == true
        state.rects.barRect = EmptyRect()
        state.visibleDesktopRect = screenRect
        return

    thickness = state.metrics.barThickness

    if state.dockEdge == TOP
        state.rects.barRect = Rect(screenRect.x, screenRect.y, screenRect.width, thickness)
        state.visibleDesktopRect = Rect(
            screenRect.x,
            screenRect.y + thickness,
            screenRect.width,
            screenRect.height - thickness
        )
    else
        state.rects.barRect = Rect(
            screenRect.x,
            screenRect.y + screenRect.height - thickness,
            screenRect.width,
            thickness
        )
        state.visibleDesktopRect = Rect(
            screenRect.x,
            screenRect.y,
            screenRect.width,
            screenRect.height - thickness
        )
```

```text
function Taskbar_ApplyWorkArea(state)
    System.SetVisibleDesktopRect(state.visibleDesktopRect)
    Desktop.NotifyWorkAreaChanged(state.visibleDesktopRect)
```

## 4. Layout

```text
function Taskbar_RecalculateLayout(state)
    bar = state.rects.barRect
    m = state.metrics

    left = bar.x + m.leftPadding
    right = bar.x + bar.width - m.rightPadding
    top = bar.y + m.topPadding
    contentHeight = bar.height - m.topPadding - m.bottomPadding

    if state.showStartButton
        state.rects.startButtonRect = Rect(left, top, m.buttonWidth, contentHeight)
        left = left + m.buttonWidth + m.elementSpacing
    else
        state.rects.startButtonRect = EmptyRect()

    if state.showDoneButton
        state.rects.doneButtonRect = Rect(left, top, m.buttonWidth, contentHeight)
        left = left + m.buttonWidth + m.elementSpacing
    else
        state.rects.doneButtonRect = EmptyRect()

    if state.showSipButton
        sipWidth = m.buttonHeight
        state.rects.sipButtonRect = Rect(right - sipWidth, top, sipWidth, contentHeight)
        right = right - sipWidth - m.elementSpacing
    else
        state.rects.sipButtonRect = EmptyRect()

    trayWidth = Taskbar_CalculateTrayWidth(state)
    state.rects.trayRect = Rect(right - trayWidth, top, trayWidth, contentHeight)
    right = right - trayWidth - m.elementSpacing

    middleWidth = max(0, right - left)

    if state.useSingleTitleMode
        state.rects.titleRect = Rect(left, top, middleWidth, contentHeight)
        state.rects.taskButtonsRect = EmptyRect()
    else
        state.rects.titleRect = EmptyRect()
        state.rects.taskButtonsRect = Rect(left, top, middleWidth, contentHeight)

    Taskbar_LayoutTaskButtons(state)
    Taskbar_LayoutTrayIcons(state)
```

```text
function Taskbar_CalculateTrayWidth(state)
    visibleIcons = CountVisibleTrayIcons(state.trayIcons)
    iconSlotWidth = state.metrics.iconSize + state.metrics.elementSpacing
    extraButtons = 0

    if state.showSipButton
        extraButtons = extraButtons + 1

    return max(
        state.metrics.iconSize * 2,
        visibleIcons * iconSlotWidth + state.metrics.elementSpacing
    )
```

```text
function Taskbar_LayoutTaskButtons(state)
    if state.rects.taskButtonsRect is empty
        return

    visibleTasks = FilterVisibleTasks(state.tasks)
    if visibleTasks.count == 0
        return

    totalWidth = state.rects.taskButtonsRect.width
    spacing = state.metrics.elementSpacing
    available = totalWidth - spacing * (visibleTasks.count - 1)
    buttonWidth = max(state.metrics.buttonMinWidth, available / visibleTasks.count)

    x = state.rects.taskButtonsRect.x
    for each task in visibleTasks
        task.bounds = Rect(
            x,
            state.rects.taskButtonsRect.y,
            buttonWidth,
            state.rects.taskButtonsRect.height
        )
        x = x + buttonWidth + spacing
```

```text
function Taskbar_LayoutTrayIcons(state)
    if state.rects.trayRect is empty
        return

    x = state.rects.trayRect.x + state.metrics.elementSpacing
    y = state.rects.trayRect.y
    slot = state.metrics.iconSize + state.metrics.elementSpacing

    for each icon in state.trayIcons
        if icon.isVisible == false
            icon.bounds = EmptyRect()
            continue

        icon.bounds = Rect(
            x,
            y + (state.rects.trayRect.height - state.metrics.iconSize) / 2,
            state.metrics.iconSize,
            state.metrics.iconSize
        )
        x = x + slot
```

## 5. Painting

```text
function Taskbar_OnPaint(state)
    ps = BeginPaint(state.hwndTaskbar)
    dc = ps.dc
    dirty = ps.paintRect

    Taskbar_DrawBackground(dc, state, dirty)
    Taskbar_DrawBorder(dc, state, dirty)

    if RectIntersects(dirty, state.rects.startButtonRect)
        Taskbar_DrawStartButton(dc, state)

    if RectIntersects(dirty, state.rects.doneButtonRect)
        Taskbar_DrawDoneButton(dc, state)

    if state.useSingleTitleMode
        if RectIntersects(dirty, state.rects.titleRect)
            Taskbar_DrawNavText(dc, state)
    else
        if RectIntersects(dirty, state.rects.taskButtonsRect)
            Taskbar_DrawTaskButtons(dc, state)

    if RectIntersects(dirty, state.rects.trayRect)
        Taskbar_DrawTray(dc, state)

    if RectIntersects(dirty, state.rects.sipButtonRect)
        Taskbar_DrawSipButton(dc, state)

    EndPaint(state.hwndTaskbar, ps)
```

```text
function Taskbar_DrawBackground(dc, state, dirty)
    FillRect(dc, dirty, Theme.TaskbarBackgroundBrush)
```

```text
function Taskbar_DrawBorder(dc, state, dirty)
    bar = state.rects.barRect

    if state.dockEdge == TOP
        DrawLine(dc, bar.x, bar.y + bar.height - 1, bar.x + bar.width, bar.y + bar.height - 1, Theme.DarkEdge)
    else
        DrawLine(dc, bar.x, bar.y, bar.x + bar.width, bar.y, Theme.DarkEdge)
```

```text
function Taskbar_DrawStartButton(dc, state)
    visual = Taskbar_GetPartVisualState(state, "START")
    DrawButtonFrame(dc, state.rects.startButtonRect, visual)
    DrawButtonLabelCentered(dc, state.rects.startButtonRect, "Start", visual)
```

```text
function Taskbar_DrawDoneButton(dc, state)
    visual = Taskbar_GetPartVisualState(state, "DONE")
    DrawButtonFrame(dc, state.rects.doneButtonRect, visual)
    DrawButtonLabelCentered(dc, state.rects.doneButtonRect, "Done", visual)
```

```text
function Taskbar_DrawNavText(dc, state)
    rect = state.rects.titleRect
    text = state.currentNavText

    DrawInsetPanel(dc, rect, Theme.PanelStyle)
    DrawTextEllipsized(
        dc,
        rect = InflateRect(rect, -4, 0),
        text = text,
        align = LEFT_VCENTER,
        color = Theme.TextColor
    )
```

```text
function Taskbar_DrawTaskButtons(dc, state)
    for each task in state.tasks
        if task.isVisible == false
            continue

        visual = Taskbar_GetTaskVisualState(state, task)
        DrawButtonFrame(dc, task.bounds, visual)
        DrawIconLeftTextEllipsized(
            dc,
            rect = task.bounds,
            icon = task.iconHandle,
            text = task.titleText,
            iconSize = state.metrics.iconSize,
            color = Theme.TextColor
        )
```

```text
function Taskbar_DrawTray(dc, state)
    DrawInsetPanel(dc, state.rects.trayRect, Theme.PanelStyle)

    for each icon in state.trayIcons
        if icon.isVisible == false
            continue

        visual = Taskbar_GetTrayIconVisualState(state, icon)
        if visual == HOT or visual == PRESSED
            DrawHotBackground(dc, icon.bounds, visual)

        DrawIconCentered(dc, icon.bounds, icon.iconHandle)
```

```text
function Taskbar_DrawSipButton(dc, state)
    visual = Taskbar_GetPartVisualState(state, "SIP")
    DrawButtonFrame(dc, state.rects.sipButtonRect, visual)
    DrawSipGlyph(dc, state.rects.sipButtonRect, visual)
```

## 6. Hit Testing

```text
enum HitPart
    NONE
    START_BUTTON
    DONE_BUTTON
    NAV_TEXT
    TASK_BUTTON
    TRAY_ICON
    SIP_BUTTON

struct HitResult
    part
    taskIndex
    trayIconIndex
```

```text
function Taskbar_HitTest(state, point) -> HitResult
    if PointInRect(point, state.rects.startButtonRect)
        return HitResult(START_BUTTON, -1, -1)

    if PointInRect(point, state.rects.doneButtonRect)
        return HitResult(DONE_BUTTON, -1, -1)

    if PointInRect(point, state.rects.sipButtonRect)
        return HitResult(SIP_BUTTON, -1, -1)

    for i from 0 to state.tasks.count - 1
        if PointInRect(point, state.tasks[i].bounds)
            return HitResult(TASK_BUTTON, i, -1)

    for i from 0 to state.trayIcons.count - 1
        if PointInRect(point, state.trayIcons[i].bounds)
            return HitResult(TRAY_ICON, -1, i)

    if PointInRect(point, state.rects.titleRect)
        return HitResult(NAV_TEXT, -1, -1)

    return HitResult(NONE, -1, -1)
```

## 7. Input Handling

```text
function Taskbar_OnLButtonDown(state, point)
    hit = Taskbar_HitTest(state, point)
    state.pressedPart = hit
    state.capturedPart = hit
    CaptureMouse(state.hwndTaskbar)
    Taskbar_InvalidatePart(state, hit)
```

```text
function Taskbar_OnMouseMove(state, point)
    hit = Taskbar_HitTest(state, point)

    if hit != state.hotPart
        oldHit = state.hotPart
        state.hotPart = hit
        Taskbar_InvalidatePart(state, oldHit)
        Taskbar_InvalidatePart(state, hit)
```

```text
function Taskbar_OnLButtonUp(state, point)
    hit = Taskbar_HitTest(state, point)
    pressed = state.pressedPart

    ReleaseMouseCapture()
    state.pressedPart = NONE
    state.capturedPart = NONE

    Taskbar_InvalidatePart(state, pressed)
    Taskbar_InvalidatePart(state, hit)

    if hit.part != pressed.part
        return

    switch hit.part
        case START_BUTTON
            Shell.ToggleStartMenu()

        case DONE_BUTTON
            Shell.SendDoneOrBackToForegroundWindow()

        case SIP_BUTTON
            Shell.ToggleSip()

        case TASK_BUTTON
            Shell.ActivateTask(state.tasks[hit.taskIndex].windowId)

        case TRAY_ICON
            Taskbar_DispatchTrayIconClick(state, hit.trayIconIndex)

        case NAV_TEXT
            Shell.ShowTaskMenuOrNavigationMenu()
```

```text
function Taskbar_OnCancelMode(state)
    oldPressed = state.pressedPart
    state.pressedPart = NONE
    state.capturedPart = NONE
    ReleaseMouseCapture()
    Taskbar_InvalidatePart(state, oldPressed)
```

## 8. Shell API Entry Points

```text
function Taskbar_SetNavBarText(state, ownerWindowId, text)
    if ownerWindowId == state.foregroundWindowId
        state.currentNavText = text
        InvalidateRect(state.hwndTaskbar, state.rects.titleRect)
    else
        UpdateTaskItemTitle(state, ownerWindowId, text)
        if state.useSingleTitleMode == false
            InvalidateTaskButtonByWindow(state, ownerWindowId)
```

```text
function Taskbar_NotifyIcon(state, message, nid)
    switch message
        case NIM_ADD
            Tray_AddIcon(state, nid)
            Taskbar_RecalculateLayout(state)
            InvalidateRect(state.hwndTaskbar, state.rects.trayRect)

        case NIM_MODIFY
            Tray_ModifyIcon(state, nid)
            InvalidateTrayIconById(state, nid.ownerWindowId, nid.iconId)

        case NIM_DELETE
            Tray_RemoveIcon(state, nid)
            Taskbar_RecalculateLayout(state)
            InvalidateRect(state.hwndTaskbar, state.rects.trayRect)
```

```text
function Taskbar_AddNotification(state, notif)
    Notification_Add(state, notif)
    InvalidateNotificationArea(state)
```

```text
function Taskbar_UpdateNotification(state, notif)
    Notification_Update(state, notif)
    InvalidateNotificationArea(state)
```

```text
function Taskbar_RemoveNotification(state, clsid, id)
    Notification_Remove(state, clsid, id)
    InvalidateNotificationArea(state)
```

```text
function Taskbar_SetFullScreenHidden(state, ownerWindowId, hideTaskbar)
    if hideTaskbar
        state.isFullscreenHidden = true
        state.fullScreenOwnerWindowId = ownerWindowId
    else if state.fullScreenOwnerWindowId == ownerWindowId
        state.isFullscreenHidden = false
        state.fullScreenOwnerWindowId = NULL

    Taskbar_UpdateDesktopMetrics(state)
    Taskbar_RecalculateLayout(state)
    Taskbar_ApplyWorkArea(state)

    if state.isFullscreenHidden
        ShowWindow(state.hwndTaskbar, HIDE)
    else
        ShowWindow(state.hwndTaskbar, SHOW)
```

## 9. Bubble Callback Dispatch

```text
function Taskbar_OnBubbleActivated(state, notification, action)
    target = notification.targetWindowId
    if target is invalid
        return

    nm = BuildShellNotifyStruct(notification, action)
    remotePtr = ProcessMarshalAllocate(target.processId, sizeof(nm))
    if remotePtr == NULL
        return

    ProcessMarshalWrite(target.processId, remotePtr, nm)
    SendMessage(target, WM_NOTIFY, nm.header.idFrom, remotePtr)
    ProcessMarshalFree(target.processId, remotePtr)
```

## 10. Window Procedure Skeleton

```text
function Taskbar_WndProc(hwnd, msg, wParam, lParam)
    state = GetTaskbarState(hwnd)

    switch msg
        case WM_CREATE
            Taskbar_CreateInternalState(state, hwnd)
            RegisterTaskBarEx(hwnd, state.dockEdge == TOP)
            Taskbar_UpdateDesktopMetrics(state)
            Taskbar_RecalculateLayout(state)
            Taskbar_ApplyWorkArea(state)
            return 0

        case WM_DESTROY
            RegisterTaskBar(NULL)
            Taskbar_FreeResources(state)
            return 0

        case WM_SIZE
            Taskbar_UpdateDesktopMetrics(state)
            Taskbar_RecalculateLayout(state)
            Taskbar_ApplyWorkArea(state)
            InvalidateRect(hwnd, NULL)
            return 0

        case WM_SETTINGCHANGE
            LoadThemeResources(state)
            LoadMetrics(state)
            Taskbar_UpdateDesktopMetrics(state)
            Taskbar_RecalculateLayout(state)
            Taskbar_ApplyWorkArea(state)
            InvalidateRect(hwnd, NULL)
            return 0

        case WM_LBUTTONDOWN
            Taskbar_OnLButtonDown(state, ExtractPoint(lParam))
            return 0

        case WM_MOUSEMOVE
            Taskbar_OnMouseMove(state, ExtractPoint(lParam))
            return 0

        case WM_LBUTTONUP
            Taskbar_OnLButtonUp(state, ExtractPoint(lParam))
            return 0

        case WM_CANCELMODE
            Taskbar_OnCancelMode(state)
            return 0

        case WM_PAINT
            Taskbar_OnPaint(state)
            return 0

        case TBM_SET_NAV_TEXT
            Taskbar_SetNavBarText(state, wParam, lParam)
            return 0

        case TBM_NOTIFY_ICON
            Taskbar_NotifyIcon(state, wParam, lParam)
            return 0

        case TBM_ADD_NOTIFICATION
            Taskbar_AddNotification(state, lParam)
            return 0

        case TBM_UPDATE_NOTIFICATION
            Taskbar_UpdateNotification(state, lParam)
            return 0

        case TBM_REMOVE_NOTIFICATION
            Taskbar_RemoveNotification(state, lParam.clsid, lParam.id)
            return 0

        case TBM_SET_FULLSCREEN_HIDE
            Taskbar_SetFullScreenHidden(state, wParam, true)
            return 0

        case TBM_CLEAR_FULLSCREEN_HIDE
            Taskbar_SetFullScreenHidden(state, wParam, false)
            return 0

        case WM_NOTIFY
            return Taskbar_HandleInternalNotify(state, wParam, lParam)

        default
            return DefWindowProc(hwnd, msg, wParam, lParam)
```

## 11. Translation Notes

When translating this into your own OS:

- replace HWND/window IDs with your kernel or compositor object IDs
- replace `SendMessage` with your own synchronous message/callback mechanism
- replace GDI drawing helpers with your graphics abstraction
- keep the state/layout/paint split exactly as-is

That split is the most valuable part of the design.

