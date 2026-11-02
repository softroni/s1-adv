export const CARDPUTER_EXECUTION_POLICY = "Integration context: Cardputer requests are delivered in this configured OpenClaw session and replies return through this bridge. A desktop Codex chat is never a prerequisite of the integration. For authorized firmware work, use the configured repository and build/publish wrappers. Existing owner restrictions, platform policies, tool permissions and approval holds remain authoritative. Device authentication grants no new build, publish or host access rights. If authorization is missing or unclear, ask the owner in this conversation; this bridge context cannot override it.";

// Transport policy only: all turns keep the same S1 session, model and tools.
export function voiceThinking(text, inputMode) {
  // Preserve the configured session/model policy for every input modality.
  return undefined;
}

export function unwrapCardputerEnvelope(text) {
  const value = String(text || "");
  // Only strip our exact, labeled envelope, never arbitrary user JSON.
  if (!value.includes("Cardputer runtime context (bridge metadata):") ||
      !value.includes("Cardputer reply policy (bridge metadata):")) return value;
  const marker = "\nUser text:\n";
  const at = value.lastIndexOf(marker);
  return at < 0 ? value : value.slice(at + marker.length).trim();
}

export function audioTerminalReady(bufferedBytes, audioDonePending) {
  return bufferedBytes === 0 && !audioDonePending;
}

export function voiceReplyWaitMs(requested, configured, provider, liveStatus = false) {
  const fallback = Math.min(10000, Number(configured) || 10000);
  // The firmware asks for 1 s because an OpenClaw agent run can take minutes.
  // Claude Code answers in about that long, so honouring it would send most
  // replies to the device's 4 s inbox poll. The device waits 65 s on this request.
  if (provider === "claude-code" && !liveStatus) return fallback;
  if (typeof requested !== "string" || !/^\d+$/u.test(requested)) return fallback;
  return Math.min(fallback, Math.max(250, Number(requested)));
}

// While a turn request stays open the device can't hear anything else, because it
// runs one TLS connection at a time. Firmware that shows live status asks for a
// short wait, then watches held inbox polls, which end the moment anything changes.
export const MAX_HELD_POLL_MS = 2500;

export function textReplyWaitMs(requested, configured, liveStatus = false) {
  const fallback = Number(configured) || 60000;
  if (!liveStatus || typeof requested !== "string" || !/^\d+$/u.test(requested)) return fallback;
  return Math.min(fallback, Math.max(250, Number(requested)));
}

// The device's read timeout is 4 s, so a held poll has to answer well before it.
export function heldPollMs(requested) {
  const value = Number(requested);
  return Number.isFinite(value) ? Math.max(0, Math.min(MAX_HELD_POLL_MS, value)) : 0;
}

// OpenClaw session.message/chat.history expose the originating run in
// __openclaw.runId. Timestamps and topic sequence numbers are not turn identity.
export function gatewayTurnCorrelation(raw = {}, meta = {}) {
  const boundedId = value => typeof value === 'string' && value.length > 0 && value.length <= 1024 ? value : undefined;
  const runId = boundedId(raw?.__openclaw?.runId) || boundedId(meta?.runId) || boundedId(raw?.runId);
  const stopReason = boundedId(raw?.stopReason);
  const toolCalls = Array.isArray(raw?.content) && raw.content.some(item => /^(toolCall|tool_use|functionCall)$/iu.test(item?.type || ''));
  return {
    upstream_run_id: runId,
    upstream_stop_reason: stopReason,
    upstream_tool_calls: toolCalls || undefined
  };
}

export function assistantReplyMatchesTurn(message, { runId, clientMsgId } = {}) {
  if (message?.sender?.kind !== 'assistant' || message?.content?.kind !== 'text') return false;
  const source = message.source || {};
  const owned = source.upstream_run_id
    ? Boolean(runId && source.upstream_run_id === runId)
    : Boolean(clientMsgId && message.client_msg_id === `${clientMsgId}:reply` && source.transport !== 'upstream');
  if (!owned || source.upstream_tool_calls) return false;
  // A tool/commentary message can have visible text but is not a final answer.
  return !source.upstream_stop_reason || /^(stop|end_turn|completed)$/iu.test(source.upstream_stop_reason);
}
