// Runs rack3d::Layout's self-test against libRack. Build and run with tests/rack3d/run.sh.
#include <cstdio>

#include <logger.hpp>
#include <rack3d/Layout.hpp>


int main() {
	// logger::logPath is empty, so the logger writes to stderr
	rack::logger::init();
	int failures = rack::rack3d::layoutSelfTest();
	std::printf("rack3d layout self-test: %d failure(s)\n", failures);
	rack::logger::destroy();
	return failures == 0 ? 0 : 1;
}
