import path from "node:path";

// What the Cardputer shows while Claude works, read from Claude Code's streamed
// events. A status comes from the tool Claude really called, never from a guess.
export const ACTIVITY_STATES = Object.freeze(["thinking", "searching", "researching", "reading",
  "coding", "working", "building", "testing", "writing"]);

const TOOL_STATES = Object.freeze({
  WebSearch: "searching", WebFetch: "researching",
  Read: "reading", Glob: "reading", Grep: "reading",
  Edit: "coding", Write: "coding", NotebookEdit: "coding",
  Bash: "working"
});

const BUILD_COMMAND = /\b(cardputer\.py\s+(build|upload)|platformio|pio\s+run|npm\s+run\s+build|xcodebuild|cargo\s+build|make)\b/u;
const TEST_COMMAND = /(\btest_[\w-]+\.py|_smoke\.py|\bpytest\b|\bnpm\s+(run\s+)?test\b|\bnode\s+--test\b|\bcargo\s+test\b)/u;
const MAX_DETAIL_CHARS = 48;

export function shortDetail(value) {
  if (typeof value !== "string") return "";
  const clean = value.replace(/[\p{Cc}\p{Cf}]+/gu, " ").replace(/\s+/gu, " ").trim();
  return Array.from(clean).slice(0, MAX_DETAIL_CHARS).join("").trim();
}

// The device shows this line, so each case picks something safe to show. A
// command can hold a secret, so Bash shows Claude's own one-line description.
function detailFor(name, input = {}) {
  switch (name) {
    case "WebSearch": return shortDetail(input.query);
    case "WebFetch":
      try { return shortDetail(new URL(input.url).hostname); } catch { return ""; }
    case "Read": case "Edit": case "Write":
      return shortDetail(path.basename(String(input.file_path || "")));
    case "NotebookEdit": return shortDetail(path.basename(String(input.notebook_path || "")));
    case "Glob": case "Grep": return shortDetail(input.pattern);
    case "Bash": return shortDetail(input.description);
    default: return "";
  }
}

function toolActivity(name, input) {
  let state = TOOL_STATES[name] || "working";
  if (name === "Bash" && typeof input?.command === "string") {
    if (TEST_COMMAND.test(input.command)) state = "testing";
    else if (BUILD_COMMAND.test(input.command)) state = "building";
  }
  return { state, detail: input ? detailFor(name, input) : "" };
}

// One line of `claude --output-format stream-json`. Returns null when the line
// doesn't change what Claude is doing.
export function activityFromEvent(event) {
  if (event?.type === "stream_event" && event.event?.type === "content_block_start") {
    const block = event.event.content_block;
    if (block?.type === "tool_use") return toolActivity(block.name);
    if (block?.type === "text") return { state: "writing", detail: "" };
    if (block?.type === "thinking") return { state: "thinking", detail: "" };
    return null;
  }
  const content = event?.message?.content;
  if (!Array.isArray(content)) return null;
  if (event.type === "assistant") {
    // The finished call carries its input, so this adds the detail and sorts Bash.
    const tool = content.findLast(block => block?.type === "tool_use");
    return tool ? toolActivity(tool.name, tool.input || {}) : null;
  }
  if (event.type === "user" && content.some(block => block?.type === "tool_result"))
    return { state: "thinking", detail: "" };
  return null;
}

// Claude can run tools side by side, and one finishing doesn't mean it's thinking again.
export class ActivityReader {
  running = new Set();

  read(event) {
    const content = Array.isArray(event?.message?.content) ? event.message.content : [];
    for (const block of content) {
      if (event.type === "assistant" && block?.type === "tool_use" && block.id) this.running.add(block.id);
      if (event.type === "user" && block?.type === "tool_result") this.running.delete(block.tool_use_id);
    }
    const activity = activityFromEvent(event);
    return activity?.state === "thinking" && this.running.size ? null : activity;
  }
}
