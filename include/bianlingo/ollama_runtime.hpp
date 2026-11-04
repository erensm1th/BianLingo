#pragma once
#include "bianlingo/core.hpp"
namespace bl {
void ensure_portable_ollama(StopToken cancel = {});
void pause_portable_ollama(bool paused);
void stop_portable_ollama();
}
