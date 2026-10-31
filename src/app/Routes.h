#pragma once

#include "app/AppContext.h"

#include <httplib.h>

namespace wb::app {

void registerSystemRoutes(httplib::Server& server, AppContext& ctx);
void registerGeometryRoutes(httplib::Server& server, AppContext& ctx);
void registerThermalRoutes(httplib::Server& server, AppContext& ctx);
void registerPerformanceRoutes(httplib::Server& server, AppContext& ctx);
void registerHardwareRoutes(httplib::Server& server, AppContext& ctx);
void registerBackpropRoutes(httplib::Server& server, AppContext& ctx);
void registerStaticFiles(httplib::Server& server, AppContext& ctx);  // catch-all GET, must be registered last

inline void registerAllRoutes(httplib::Server& server, AppContext& ctx) {
    registerSystemRoutes(server, ctx);
    registerGeometryRoutes(server, ctx);
    registerThermalRoutes(server, ctx);
    registerPerformanceRoutes(server, ctx);
    registerHardwareRoutes(server, ctx);
    registerBackpropRoutes(server, ctx);
    registerStaticFiles(server, ctx);
}

}  // namespace wb::app
