#pragma once
#include "bianlingo/chat_style.hpp"

namespace bl {
Json default_prompt_settings(std::string_view ui_locale);
// Chinese dictionary values are output text only for the matching target script.
std::optional<std::string> local_incoming_translation(const Json &settings, std::string_view text,
                                                     const std::vector<Term> &matches, const Glossary &glossary);
// Shared by online LLMs and Ollama; dedicated translation workers do not use it.
std::string translation_prompt(const Json &settings, bool incoming, std::string_view style,
                               std::string_view target, const ProtectedLinks &links,
                               const std::vector<Term> &matches, const Json &outbound_terms);
} // namespace bl
