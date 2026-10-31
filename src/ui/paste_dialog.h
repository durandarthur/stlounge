#pragma once
// "Add shader" dialog: the tabbed editor plus name/author, filled from the
// clipboard (ShaderToy JSON into all tabs, or GLSL into the Image tab).

#include <string>
#include <vector>

#include "shadertoy/shader.h"
#include "ui/shader_editor.h"

namespace sp {

class PasteDialog {
public:
    enum class Result { None, Try, Save, Cancel, Fetch };

    // Opens the dialog, filled from `clipboard` when it holds shader JSON or code.
    void open(const std::string& clipboard);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }

    // Draws the window; returns the button pressed this frame.
    Result draw();

    // The shader as currently edited (name/author applied).
    Shader shader() const;
    // If the dialog was filled from JSON and the tabs weren't changed, the
    // original JSON text (to store losslessly); otherwise empty.
    std::string unchangedOriginalJson() const;

    // Messages shown in the dialog (e.g. why saving failed).
    void setStatus(const std::string& message) { status_ = message; }

    // The "Link or ID" field, and the fetch state shown next to it.
    const std::string& linkInput() const { return link_; }
    void setFetching(bool fetching) { fetching_ = fetching; }
    // Fills the tabs from fetched JSON, like a JSON paste. False if it isn't a shader.
    bool fillFromJson(const std::string& text, const std::string& note, const std::string& source);
    // What the library records as the shader's origin when saved unchanged.
    const std::string& source() const { return source_; }

    // Exposed for tests: what open() decided.
    const std::vector<std::string>& notes() const { return notes_; }
    const ShaderEditor& editor() const { return editor_; }

private:
    bool open_ = false;
    ShaderEditor editor_;
    std::string name_, author_;
    ShaderInfo info_;
    std::vector<std::string> notes_;  // import warnings and hints
    std::string originalJson_;         // when filled from JSON
    std::string originalPasses_;       // canonical passes of that JSON, to detect edits
    std::string status_;
    std::string link_;
    std::string source_ = "import";
    bool fetching_ = false;

    bool fillJson(const std::string& text);
    void reset();
};

}  // namespace sp
