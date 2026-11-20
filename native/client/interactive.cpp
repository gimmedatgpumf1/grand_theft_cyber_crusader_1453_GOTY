#include "client/interactive.h"

#include "client/interactive_app.h"

namespace gtc::native {

using interactive_detail::App;

int runInteractive(const InteractiveOptions& options) {
    App app(options);
    return app.run();
}

}  // namespace gtc::native
