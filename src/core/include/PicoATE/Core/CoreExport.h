#pragma once

#include <QtCore/qglobal.h>

#if defined(PICOATE_CORE_BUILD)
#define PICOATE_CORE_EXPORT Q_DECL_EXPORT
#else
#define PICOATE_CORE_EXPORT Q_DECL_IMPORT
#endif
