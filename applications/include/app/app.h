#ifndef ROS_APP_ALL_H
#define ROS_APP_ALL_H

#include "user_runtime.h"

#if defined(ROS_APP_WITH_CRT)
#include "crt.h"
#endif

#include "app/kernel.h"
#include "app/kernel_event.h"
#include "app/kernel_gui.h"
#include "app/user_ipc.h"

#if !defined(ROS_APP_WITH_CRT)
#include "app/core_log.h"
#include "app/system_ext.h"
#endif

#if defined(ROS_APP_USE_EXPLORER_SHELL)
#include "app/explorer_shell.h"
#endif

#if defined(ROS_APP_USE_WINDOW) || defined(ROS_APP_USE_GDI) || defined(ROS_APP_USE_WIDGETS)
#include "app/window.h"
#endif

#if defined(ROS_APP_USE_GDI)
#include "app/gdi.h"
#endif

#if defined(ROS_APP_USE_WIDGETS)
#include "app/widgets.h"
#endif

#if defined(ROS_APP_USE_PNG)
#include "app/png.h"
#endif

#endif
