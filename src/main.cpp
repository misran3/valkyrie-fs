#include "config.hpp"
#include "fuse_ops.hpp"
#include <iostream>
#include <csignal>
#include <memory>

using namespace valkyrie;

static std::unique_ptr<FuseContext> g_context;

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down...\n";
    if (g_context) {
        g_context->stop();
    }
    exit(signum);
}

int main(int argc, char* argv[]) {
    std::cout << "Valkyrie-FS v0.1.0\n";

    // Parse configuration
    Config config;
    if (!config.parse(argc, argv)) {
        return 1;
    }

    // Create FUSE context
    g_context = std::make_unique<FuseContext>(config);

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Define FUSE operations
    struct fuse_operations ops = {};
    ops.init = fuse_ops::init;
    ops.destroy = fuse_ops::destroy;
    ops.getattr = fuse_ops::getattr;
    ops.readdir = fuse_ops::readdir;
    ops.open = fuse_ops::open;
    ops.read = fuse_ops::read;
    ops.release = fuse_ops::release;

    // Build FUSE args
    struct fuse_args fuse_argv = FUSE_ARGS_INIT(0, NULL);
    fuse_opt_add_arg(&fuse_argv, argv[0]);
    fuse_opt_add_arg(&fuse_argv, config.mount_point.c_str());
    fuse_opt_add_arg(&fuse_argv, "-f");  // Foreground mode
    fuse_opt_add_arg(&fuse_argv, "-o");
    fuse_opt_add_arg(&fuse_argv, "ro");  // Read-only

    // Run FUSE main loop
    int ret = fuse_main(fuse_argv.argc, fuse_argv.argv, &ops, g_context.get());

    fuse_opt_free_args(&fuse_argv);

    return ret;
}
