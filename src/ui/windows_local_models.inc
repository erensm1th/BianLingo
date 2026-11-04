#include "windows_model_catalog.inc"
#include "windows_models_checks.inc"
void launch_model_job(LocalModelStore store, std::vector<std::string> chosen, BergamotModels::Fetch fetch = {}) {
    model_task = std::make_shared<LocalModelTask>();
    model_progress_caption.clear(); set(model_progress, "Verifying and preparing language packs…");
    model_worker = std::jthread([task = model_task, store = std::move(store), chosen = std::move(chosen), fetch = std::move(fetch)](std::stop_token stop) mutable {
        try {
            store.prepare(chosen, stop, [&](const ModelProgress &p) { std::lock_guard lock(task->mutex); task->progress = p; }, true, fetch);
        } catch (const std::exception &e) { std::lock_guard lock(task->mutex); task->error = e.what(); }
        std::lock_guard lock(task->mutex); task->done = true;
    });
    update_model_controls();
}
void cancel_model_job() {
    if (!model_task) return;
    model_worker.request_stop();
    set(model_progress, "Cancelling. Completed files are kept for a later retry.");
    update_model_controls();
}
void poll_model_job() {
    if (!model_task) return;
    ModelProgress p; std::string error; bool done;
    { std::lock_guard lock(model_task->mutex); p = model_task->progress; error = model_task->error; done = model_task->done; }
    if (!done) {
        if (model_worker.get_stop_token().stop_requested()) return;
        auto caption = p.total ? (p.pair == "runtime" ? ui_text("Runtime") : model_pair_name(model_pack(p.pair))) + " · " + std::to_string(p.done * 100 / p.total) + "% · " +
            model_size(p.done) + " / " + model_size(p.total) : ui_text("Verifying and preparing language packs…");
        if (caption != model_progress_caption) { model_progress_caption = caption; set(model_progress, caption); }
        return;
    }
    bool cancelled = model_worker.get_stop_token().stop_requested();
    if (model_worker.joinable()) model_worker.join();
    model_task.reset();
    set(model_progress, error.empty() ? "Language packs are ready. You can start playing." : cancelled ?
        "Cancelled. Completed files are kept and skipped on retry." : ui_text("Language pack preparation failed: ") + ui_text(error));
    try { refresh_model_inventory(); } catch (const std::exception &e) { set(model_progress, e.what()); }
    update_model_controls();
}
