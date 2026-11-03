// The Lab's sequences: the list, the screen that changes one, and the one that plays it. What runs
// them is lab::SeqRun, kept going by part 081. A step is changed on a copy of its sequence, and the
// whole thing is saved on the way out, so a slip of Esc can't lose a long list.
namespace {
enum class LabSeqAsk : uint8_t { What, How, Value, Wait, Name };
constexpr int16_t kLabSeqFirstWait = 10;  // A second.
constexpr int kLabSeqPlayRows = 5, kLabSeqPlayRowH = 18;
constexpr int kLabSeqOfferRows = 6, kLabSeqOfferRowH = 15;

struct LabSeqEdit {
  lab::Sequence seq;
  int index = -1;               // Which saved sequence this is, or -1 until a new one is kept.
  bool offered = false;         // From Claude, so only Enter on its review screen saves it.
  int row = 0, top = 0;
  bool deleteArmed = false;
  bool nameTouched = false;
  // The step being asked about.
  lab::SeqStep step;
  int stepAt = 0;
  bool adding = false;
  LabSeqAsk ask = LabSeqAsk::What;
  int askRow = 0, askTop = 0;
} gLabSeqEdit;

// --- Words ------------------------------------------------------------------------------------
String labWaitText(int tenths) {
  if (tenths < 600) return labHoldText(static_cast<uint16_t>(tenths * 100));
  const int minutes = tenths / 60;  // In tenths of a minute.
  return String(minutes / 10) + (minutes % 10 ? String(".") + String(minutes % 10) : String("")) + " min";
}
String labStepText(const lab::SeqStep& step) {
  if (step.kind == lab::SeqKind::Wait) return String("Wait ") + labWaitText(step.value);
  const lab::OutInfo& info = lab::info(step.out);
  if (step.kind == lab::SeqKind::Off) return String(lab::isStrip(step.out) ? "Strip" : info.name) + " off";
  if (info.onOff) return String(info.name) + (step.value ? " on" : " off");
  return String(info.name) + " to " + labOutText(step.out, step.value);
}
int labSeqRows() { return gLabSeqCount < lab::kMaxSequences ? gLabSeqCount + 1 : gLabSeqCount; }
bool labSeqHasAddRow() { return gLabSeqEdit.seq.count < lab::kMaxSeqSteps; }
// The steps, then adding one, then how it plays, then its name.
int labSeqEditRows() { return gLabSeqEdit.seq.count + (labSeqHasAddRow() ? 3 : 2); }

// --- The list ----------------------------------------------------------------------------------
void renderLabSeqs() {
  const LabUi& ui = gLabUi;
  labHead("Sequences", launcher_icons::kSteps, kLabLime, String(int(gLabSeqCount)) + " SAVED", gLabSaved ? kLabSoft : kLabAmber);
  const int rows = labSeqRows();
  for (int row = 0; row < min(kLabListRows, rows); ++row) {
    const int i = ui.seqTop + row;
    const int y = kLabListY + row * kLabRowH;
    const bool chosen = i == ui.seqRow;
    if (chosen) labRowBar(y, 233, kLabLime);
    if (i >= gLabSeqCount) {
      labTextAt(&fonts::FreeSansBold9pt7b, 8, y + 2, "+ New sequence", chosen ? TFT_BLACK : kLabLime);
      continue;
    }
    const lab::Sequence& seq = gLabSeqs[i];
    labTextAt(&fonts::FreeSansBold9pt7b, 8, y + 2, seq.name, chosen ? TFT_BLACK : TFT_WHITE);
    labTextRight(&fonts::Font0, 228, y + 6, String(int(seq.count)) + (seq.count == 1 ? " step" : " steps") + (seq.loop ? ", loops" : ""), chosen ? TFT_BLACK : kLabMuted);
  }
  labScrollBar(ui.seqTop, kLabListRows, rows, kLabListY, kLabListRows * kLabRowH - 1);
  if (!gLabSeqCount) {
    labTextAt(&fonts::Font0, 6, 52, "Steps that run in order, with waits", kLabSoft);
    labTextAt(&fonts::Font0, 6, 63, "between them. Rules can start them.", kLabSoft);
  }
  if (!ui.seqDeleteArmed) {
    labKeys("ENTER edit  P play  X del  ALT ask");
    return;
  }
  const int users = lab::sequenceUsers(gLabBook.rules, gLabBook.count, static_cast<uint8_t>(ui.seqRow));
  if (!users) labKeys("X again deletes this sequence");
  else labKeys(String("X again deletes it. ") + String(users) + (users == 1 ? " rule uses it" : " rules use it"));
}

// --- Changing one ------------------------------------------------------------------------------
void renderLabSeqEdit() {
  auto& d = gCanvas;
  const LabSeqEdit& e = gLabSeqEdit;
  labHead(e.seq.name, launcher_icons::kSteps, kLabLime, e.offered ? "NOT SAVED" : "SAVED ON ESC", e.offered ? kLabAmber : kLabSoft);
  const int rows = labSeqEditRows(), steps = e.seq.count;
  const int addRow = labSeqHasAddRow() ? steps : -1;
  for (int row = 0; row < min(kLabListRows, rows); ++row) {
    const int i = e.top + row;
    const int y = kLabListY + row * kLabRowH;
    const bool chosen = i == e.row;
    const uint16_t ink = chosen ? TFT_BLACK : TFT_WHITE;
    if (chosen) labRowBar(y, 233, kLabLime);
    if (i < steps) {
      const lab::SeqStep& step = e.seq.steps[i];
      const bool hue = step.kind == lab::SeqKind::Set && (step.out == Out::StripColor || step.out == Out::Led);
      labTextAt(&fonts::Font0, 6, y + 7, String(i + 1), chosen ? TFT_BLACK : kLabMuted);
      gCanvas.setFont(&fonts::Font2);
      labTextAt(&fonts::Font2, 24, y + 2, fitCurrentFontToWidth(labStepText(step), hue ? 184 : 204), step.kind == lab::SeqKind::Wait && !chosen ? kLabCyan : ink);
      if (hue) d.fillRoundRect(212, y + 5, 16, 11, 3, labHueColor(step.value));
    } else if (i == addRow) {
      labTextAt(&fonts::Font2, 24, y + 2, "+ Add a step", chosen ? TFT_BLACK : kLabLime);
    } else if (i == rows - 2) {
      labTextAt(&fonts::Font2, 8, y + 2, "Plays", chosen ? TFT_BLACK : kLabMuted);
      labTextAt(&fonts::Font2, 58, y + 2, e.seq.loop ? "Over and over" : "Once", ink);
    } else {
      labTextAt(&fonts::Font2, 8, y + 2, "Name", chosen ? TFT_BLACK : kLabMuted);
      labTextAt(&fonts::Font2, 58, y + 2, e.seq.name, ink);
    }
  }
  labScrollBar(e.top, kLabListRows, rows, kLabListY, kLabListRows * kLabRowH - 1);
  if (e.deleteArmed) labKeys("X again deletes this step");
  else if (e.row < steps) labKeys(String("ENTER edit ") + kLabArrowLeftRight + (e.offered ? " move A add X del" : " move A add X del P play"));
  else labKeys(e.offered ? "ENTER change  ESC back" : "ENTER change  P play  ESC save");
}
void labSeqOpen(int index) {
  LabSeqEdit& e = gLabSeqEdit;
  e = LabSeqEdit{};
  e.index = index;
  e.nameTouched = index >= 0;
  if (index >= 0) e.seq = gLabSeqs[index];
  else snprintf(e.seq.name, sizeof(e.seq.name), "Sequence %d", gLabSeqCount + 1);
  gLabUi.view = LabView::SeqEdit;
}
// A new one with nothing in it isn't worth keeping.
void labSeqKeep() {
  LabSeqEdit& e = gLabSeqEdit;
  if (e.index < 0 && !e.seq.count) return;
  if (!e.seq.name[0]) snprintf(e.seq.name, sizeof(e.seq.name), "Sequence %d", e.index < 0 ? gLabSeqCount + 1 : e.index + 1);
  if (e.index >= 0 && !memcmp(&gLabSeqs[e.index], &e.seq, sizeof(e.seq))) return;
  if (e.index < 0) e.index = gLabSeqCount++;
  gLabSeqs[e.index] = e.seq;
  labSaveSeqs();
}
void labSeqShowInList(int index) {
  LabUi& ui = gLabUi;
  ui.seqRow = lab::clampTo(index, 0, labSeqRows() - 1);
  ui.seqTop = 0;
  labKeepRowShown(ui.seqRow, ui.seqTop, kLabListRows);
}
void labSeqLeaveEdit() {
  if (gLabSeqEdit.offered) {  // Back to be looked at, not saved.
    gLabSeqEdit.top = 0;
    gLabUi.view = LabView::SeqOffer;
    return;
  }
  labSeqKeep();
  labSeqShowInList(gLabSeqEdit.index >= 0 ? gLabSeqEdit.index : gLabUi.seqRow);
  gLabUi.view = LabView::Seqs;
}
void labSeqAskAbout(const lab::SeqStep& step, int at, bool adding) {
  LabSeqEdit& e = gLabSeqEdit;
  e.step = step;
  e.stepAt = at;
  e.adding = adding;
  e.ask = LabSeqAsk::What;
  e.askRow = adding || step.kind == lab::SeqKind::Wait ? 0 : static_cast<int>(step.out) + 1;
  e.askTop = 0;
  labKeepRowShown(e.askRow, e.askTop, kLabWizardRows);
  gLabUi.view = LabView::SeqAsk;
}
void labSeqPlay(int index, LabView back) {
  LabUi& ui = gLabUi;
  if (!gLabSeqs[index].count) {
    setStatus("Add a step first", 1200);
    return;
  }
  ui.seqShown = index;
  ui.seqBack = back;
  labReleaseOutputs();
  gLabSeqRun.start(static_cast<uint8_t>(index), -1, millis());
  ui.view = LabView::SeqPlay;
}
void labSeqPlayAgain() { labSeqPlay(gLabUi.seqShown, gLabUi.seqBack); }
void labSeqsKey(char lower, bool up, bool down, bool enter) {
  LabUi& ui = gLabUi;
  const int rows = labSeqRows();
  const bool onSeq = ui.seqRow < gLabSeqCount;
  if (lower != 'x') ui.seqDeleteArmed = false;
  if (up || down) {
    ui.seqRow = (ui.seqRow + (up ? rows - 1 : 1)) % rows;
    labKeepRowShown(ui.seqRow, ui.seqTop, kLabListRows);
  } else if (enter && onSeq) {
    labSeqOpen(ui.seqRow);
  } else if (enter) {
    labSeqOpen(-1);
    labSeqAskAbout(lab::SeqStep{}, 0, true);
  } else if (lower == 'p' && onSeq) {
    labSeqPlay(ui.seqRow, LabView::Seqs);
  } else if (lower == 'x' && onSeq) {
    if (ui.seqDeleteArmed) {
      lab::removeSequence(gLabSeqs, gLabSeqCount, gLabBook.rules, gLabBook.count, static_cast<uint8_t>(ui.seqRow));
      labSaveSeqs();
      labSaveBook();
      labSeqShowInList(ui.seqRow);
    }
    ui.seqDeleteArmed = !ui.seqDeleteArmed;
  }
}
void labSeqEditKey(char lower, bool left, bool right, bool up, bool down, bool enter) {
  LabSeqEdit& e = gLabSeqEdit;
  lab::Sequence& seq = e.seq;
  const int rows = labSeqEditRows(), steps = seq.count;
  const bool onStep = e.row < steps;
  if (lower != 'x') e.deleteArmed = false;
  if (up || down) {
    e.row = (e.row + (up ? rows - 1 : 1)) % rows;
  } else if ((left || right) && onStep) {
    const int to = e.row + (right ? 1 : -1);
    if (to >= 0 && to < steps) {
      const lab::SeqStep moved = seq.steps[e.row];
      seq.steps[e.row] = seq.steps[to];
      seq.steps[to] = moved;
      e.row = to;
    }
  } else if (lower == 'a' && labSeqHasAddRow()) {
    labSeqAskAbout(lab::SeqStep{}, onStep ? e.row + 1 : steps, true);
  } else if (lower == 'p' && e.offered) {
    setStatus("Save it first: Esc, then Enter", 1500);
  } else if (lower == 'p') {
    labSeqKeep();
    if (e.index >= 0) labSeqPlay(e.index, LabView::SeqEdit);
    else setStatus("Add a step first", 1200);
  } else if (lower == 'x' && onStep) {
    if (e.deleteArmed) {
      for (int i = e.row; i + 1 < steps; ++i) seq.steps[i] = seq.steps[i + 1];
      --seq.count;
    }
    e.deleteArmed = !e.deleteArmed;
  } else if (enter && onStep) {
    labSeqAskAbout(seq.steps[e.row], e.row, false);
  } else if (enter && e.row == rows - 2) {
    seq.loop = !seq.loop;
  } else if (enter && e.row == rows - 1) {
    e.ask = LabSeqAsk::Name;
    gLabUi.view = LabView::SeqAsk;
  } else if (enter) {
    labSeqAskAbout(lab::SeqStep{}, steps, true);
  }
  e.top = min(e.top, max(labSeqEditRows() - kLabListRows, 0));
  labKeepRowShown(e.row, e.top, kLabListRows);
}

// --- One step ----------------------------------------------------------------------------------
int labSeqAskRows() { return gLabSeqEdit.ask == LabSeqAsk::What ? 1 + lab::kSeqOutCount : 2; }
String labSeqAskRow(int i) {
  const LabSeqEdit& e = gLabSeqEdit;
  if (e.ask == LabSeqAsk::What) return i ? lab::kOuts[i - 1].name : "Wait";
  if (lab::info(e.step.out).onOff) return i ? "turn off" : "turn on";
  return i ? "turn off" : "set to a value";
}
void renderLabSeqAsk() {
  auto& d = gCanvas;
  const LabSeqEdit& e = gLabSeqEdit;
  const bool naming = e.ask == LabSeqAsk::Name;
  labHead(e.seq.name, launcher_icons::kSteps, kLabLime, naming ? String("NAME") : String("STEP ") + String(e.stepAt + 1), kLabSoft);
  if (naming) {
    labTextAt(&fonts::Font2, 4, 20, "Give it a name", kLabCyan);
    d.fillRoundRect(4, 46, 232, 28, 6, kLabPanel);
    labTextAt(&fonts::FreeSansBold9pt7b, 12, 51, String(e.seq.name) + (e.nameTouched ? "_" : ""), e.nameTouched ? TFT_WHITE : kLabMuted);
    labKeys("Type a name  ENTER done  ESC back");
    return;
  }
  if (e.ask == LabSeqAsk::Value) {
    labDrawOutValue(e.step.out, e.step.value);
    return;
  }
  if (e.ask == LabSeqAsk::Wait) {
    labTextAt(&fonts::Font2, 4, 20, "How long should it wait?", kLabCyan);
    labTextCenter(&fonts::Font4, 120, 48, labWaitText(e.step.value), TFT_WHITE);
    labTextCenter(&fonts::Font0, 120, 88, "What's on stays on while it waits", kLabSoft);
    labKeys(String(kLabArrowLeftRight) + " change  ENTER use it  ESC back");
    return;
  }
  labTextAt(&fonts::Font2, 4, 20, e.ask == LabSeqAsk::What ? "What should this step do?" : lab::info(e.step.out).name, kLabCyan);
  const int rows = labSeqAskRows();
  for (int row = 0; row < min(kLabWizardRows, rows); ++row) {
    const int i = e.askTop + row;
    const int y = 37 + row * 22;
    const bool chosen = i == e.askRow;
    if (chosen) d.fillRoundRect(1, y, 233, 21, 6, kLabLime);
    labTextAt(&fonts::FreeSansBold9pt7b, 8, y + 2, labSeqAskRow(i), chosen ? TFT_BLACK : TFT_WHITE);
  }
  labScrollBar(e.askTop, kLabWizardRows, rows, 37, kLabWizardRows * 22 - 1);
  labKeys(String(kLabArrowUpDown) + " pick  ENTER next  ESC back");
}
void labSeqAskDone() {
  LabSeqEdit& e = gLabSeqEdit;
  lab::Sequence& seq = e.seq;
  if (!e.adding) {
    seq.steps[e.stepAt] = e.step;
  } else if (seq.count < lab::kMaxSeqSteps) {
    for (int i = seq.count; i > e.stepAt; --i) seq.steps[i] = seq.steps[i - 1];
    seq.steps[e.stepAt] = e.step;
    ++seq.count;
    e.row = e.stepAt + 1;  // After the last step that's "+ Add a step", so another is one key away.
  }
  labKeepRowShown(e.row, e.top, kLabListRows);
  gLabUi.view = LabView::SeqEdit;
}
void labSeqAskValue() {
  LabSeqEdit& e = gLabSeqEdit;
  const lab::OutInfo& info = lab::info(e.step.out);
  e.step.kind = lab::SeqKind::Set;
  e.step.value = lab::clampTo<int16_t>(e.step.value, info.lo, info.hi);
  e.ask = LabSeqAsk::Value;
}
void labSeqAskPick() {
  LabSeqEdit& e = gLabSeqEdit;
  lab::SeqStep& step = e.step;
  if (e.ask == LabSeqAsk::How) {
    const bool first = e.askRow == 0;
    if (lab::info(step.out).onOff) {
      step.kind = lab::SeqKind::Set;
      step.value = first ? 1 : 0;
      labSeqAskDone();
    } else if (first) {
      labSeqAskValue();
    } else {
      step.kind = lab::SeqKind::Off;
      labSeqAskDone();
    }
    return;
  }
  if (e.askRow == 0) {
    if (step.kind != lab::SeqKind::Wait) step.value = kLabSeqFirstWait;
    step.kind = lab::SeqKind::Wait;
    e.ask = LabSeqAsk::Wait;
    return;
  }
  const Out out = static_cast<Out>(e.askRow - 1);
  const lab::OutInfo& info = lab::info(out);
  if (step.kind == lab::SeqKind::Wait || step.out != out) {
    step.kind = lab::SeqKind::Set;
    step.value = info.onOff ? 1 : static_cast<int16_t>((info.lo + info.hi) / 2 / info.step * info.step);
  }
  step.out = out;
  if (!lab::canTurnOff(out)) {
    labSeqAskValue();
    return;
  }
  e.ask = LabSeqAsk::How;
  e.askRow = info.onOff ? (step.value ? 0 : 1) : (step.kind == lab::SeqKind::Off ? 1 : 0);
  e.askTop = 0;
}
void labSeqAskBack() {
  LabSeqEdit& e = gLabSeqEdit;
  if (e.ask == LabSeqAsk::Name || e.ask == LabSeqAsk::What) {
    gLabUi.view = LabView::SeqEdit;
    return;
  }
  const bool toHow = e.ask == LabSeqAsk::Value && lab::canTurnOff(e.step.out);
  e.ask = toHow ? LabSeqAsk::How : LabSeqAsk::What;
  e.askRow = toHow ? 0 : e.step.kind == lab::SeqKind::Wait ? 0 : static_cast<int>(e.step.out) + 1;
  e.askTop = 0;
  labKeepRowShown(e.askRow, e.askTop, kLabWizardRows);
}
void labSeqAskKey(char key, bool left, bool right, bool up, bool down, bool enter, bool del) {
  LabSeqEdit& e = gLabSeqEdit;
  switch (e.ask) {
    case LabSeqAsk::Name:
      if (enter) {
        gLabUi.view = LabView::SeqEdit;
      } else {
        // A suggestion is the name until the first letter, unlike a saved name, which can be corrected.
        const char was = e.seq.name[0];
        if (!e.nameTouched) e.seq.name[0] = '\0';
        labTypeName(e.seq.name, e.nameTouched, key, del);
        if (!e.nameTouched) e.seq.name[0] = was;
      }
      break;
    case LabSeqAsk::Value: {
      const lab::OutInfo& info = lab::info(e.step.out);
      if (left || right) e.step.value = lab::clampTo<int16_t>(e.step.value + (right ? info.step : -info.step), info.lo, info.hi);
      else if (enter) labSeqAskDone();
      break;
    }
    case LabSeqAsk::Wait:
      if (left || right) e.step.value = lab::nextWait(e.step.value, right ? 1 : -1);
      else if (enter) labSeqAskDone();
      break;
    default:
      if (up || down) {
        const int rows = labSeqAskRows();
        e.askRow = (e.askRow + (up ? rows - 1 : 1)) % rows;
        labKeepRowShown(e.askRow, e.askTop, kLabWizardRows);
      } else if (enter) {
        labSeqAskPick();
      }
      break;
  }
}

// --- One from Claude ---------------------------------------------------------------------------
// It waits here for Enter, like a rule, because a step can switch mains. It sits on the edit
// screen's copy, so E is all it takes to change it, and it costs no memory of its own. Changing it
// doesn't save it or let it play: Esc comes back here, and only Enter here is a yes.
bool labSameStep(const lab::SeqStep& a, const lab::SeqStep& b) { return a.kind == b.kind && a.value == b.value && (a.kind == lab::SeqKind::Wait || a.out == b.out); }
void renderLabSeqOffer() {
  const LabSeqEdit& e = gLabSeqEdit;
  // A change greys out what stayed the same, so what's different is what stands out.
  const lab::Sequence* was = e.index >= 0 ? &gLabSeqs[e.index] : nullptr;
  labHead(was ? "Claude changed this" : "Claude wrote this", launcher_icons::kSteps, kLabLime, "", kLabSoft);
  gCanvas.setFont(&fonts::Font2);
  for (int row = 0; row < min(kLabSeqOfferRows, static_cast<int>(e.seq.count)); ++row) {
    const int i = e.top + row;
    const int y = 20 + row * kLabSeqOfferRowH;
    const lab::SeqStep& step = e.seq.steps[i];
    const bool same = was && i < was->count && labSameStep(step, was->steps[i]);
    labTextAt(&fonts::Font0, 6, y + 4, String(i + 1), kLabMuted);
    const bool hue = step.kind == lab::SeqKind::Set && (step.out == Out::StripColor || step.out == Out::Led);
    labTextAt(&fonts::Font2, 24, y, fitCurrentFontToWidth(labStepText(step), hue ? 184 : 206), same ? kLabMuted : step.kind == lab::SeqKind::Wait ? kLabCyan : kLabLime);
    if (hue) gCanvas.fillRoundRect(212, y + 2, 16, 11, 3, labHueColor(step.value));
  }
  labScrollBar(e.top, kLabSeqOfferRows, e.seq.count, 20, kLabSeqOfferRows * kLabSeqOfferRowH);
  // The rows can't show a name, a loop or steps that went, and grey has to mean the same.
  const bool same = !was || (was->count == e.seq.count && was->loop == e.seq.loop && !strcmp(was->name, e.seq.name));
  const String fewer = was && was->count != e.seq.count ? String(e.seq.name) + (e.seq.loop ? ", loops" : ", once") + ", was " + String(int(was->count)) + " steps" : String("");
  labTextAt(&fonts::Font0, 6, 117, !fewer.isEmpty() ? fewer : String("Name: ") + e.seq.name + (e.seq.loop ? ", loops" : ", plays once"), same ? kLabMuted : kLabLime);
  labKeys(e.seq.count > kLabSeqOfferRows ? String(kLabArrowUpDown) + " more  ENTER save  E edit  ESC drop" : String("ENTER save  E edit  ESC drop it"));
}
void labSeqOfferKey(char lower, bool up, bool down, bool enter) {
  LabSeqEdit& e = gLabSeqEdit;
  const int last = max(static_cast<int>(e.seq.count) - kLabSeqOfferRows, 0);
  if (up || down) {
    e.top = lab::clampTo(e.top + (up ? -1 : 1), 0, last);
  } else if (enter && e.index < 0 && gLabSeqCount >= lab::kMaxSequences) {
    setStatus("Sequences are full: delete one first", 1500);
  } else if (enter) {
    e.offered = false;
    labSeqLeaveEdit();
  } else if (lower == 'e') {
    e.top = 0;
    gLabUi.view = LabView::SeqEdit;
  }
}
// Called with the same name as a saved one, it's a change to that one. Two with one name couldn't
// be told apart when a rule asks for it.
bool labOfferSequence(const lab::Sequence& seq, const char* replaces) {
  LabUi& ui = gLabUi;
  LabSeqEdit& e = gLabSeqEdit;
  if (*replaces && lab::findByName(gLabSeqs, gLabSeqCount, replaces) < 0) return false;
  // The owner's own edit is kept, but an offer that hasn't had its Enter is only replaced.
  if (labMode(gUiMode) && !e.offered && (ui.view == LabView::SeqEdit || ui.view == LabView::SeqAsk)) labSeqKeep();
  const int index = lab::findByName(gLabSeqs, gLabSeqCount, *replaces ? replaces : seq.name);
  if (!labMode(gUiMode)) setUiMode(UiMode::LabSequences);
  labStopPlaying();
  e = LabSeqEdit{};
  e.seq = seq;
  e.index = index;
  e.offered = true;
  e.nameTouched = true;
  ui.view = LabView::SeqOffer;
  labOfferLanded();
  labRedraw();
  return true;
}

// --- Playing one -------------------------------------------------------------------------------
void renderLabSeqPlay() {
  auto& d = gCanvas;
  const lab::Sequence& seq = gLabSeqs[gLabUi.seqShown];
  const bool running = gLabSeqRun.running();
  const int at = gLabSeqRun.shown(), count = seq.count;
  const String chip = running ? String(seq.loop ? "LOOP " : "STEP ") + String(at + 1) + "/" + String(count) : String("DONE");
  labHead(seq.name, launcher_icons::kSteps, kLabLime, chip, running ? kLabGreen : kLabSoft);
  const int top = lab::clampTo(at - 2, 0, max(count - kLabSeqPlayRows, 0));
  for (int row = 0; row < min(kLabSeqPlayRows, count); ++row) {
    const int i = top + row;
    const int y = 21 + row * kLabSeqPlayRowH;
    const bool current = running && i == at;
    const lab::SeqStep& step = seq.steps[i];
    if (current) d.fillRoundRect(1, y, 237, kLabSeqPlayRowH - 1, 5, kLabGreen);
    labTextAt(&fonts::Font0, 6, y + 5, String(i + 1), current ? TFT_BLACK : kLabMuted);
    gCanvas.setFont(&fonts::Font2);
    labTextAt(&fonts::Font2, 24, y, fitCurrentFontToWidth(labStepText(step), 160), current ? TFT_BLACK : running && i < at ? kLabMuted : TFT_WHITE);
    if (current && step.kind == lab::SeqKind::Wait) labTextRight(&fonts::Font0, 232, y + 5, labWaitText(max(static_cast<int>((gLabSeqRun.leftMs(millis()) + 99) / 100), 1)) + " left", TFT_BLACK);
  }
  labKeys(running ? "ESC stop  P start again" : "ESC back  P play again");
}

// --- Played from the chat ----------------------------------------------------------------------
// A saved sequence, by name. It plays on its own screen, the only place it can be seen and stopped,
// once the reply that asked for it has finished speaking. One that switches mains opens there ready
// and waits for P, because that's a press only the owner makes.
constexpr uint32_t kLabPlayWaitMs = 30000;
char gLabPlayAsked[lab::kNameChars + 1] = "";
uint32_t gLabPlayAskedMs = 0;
bool labPlayAsked(const char* name) {
  const int index = lab::findByName(gLabSeqs, gLabSeqCount, name);
  if (index < 0 || !gLabSeqs[index].count) {
    logf("[LAB] no sequence to play called %s", name);
    setStatus("No saved sequence by that name", 2500);
    return false;
  }
  // By name, because the list can change before it plays.
  snprintf(gLabPlayAsked, sizeof(gLabPlayAsked), "%s", gLabSeqs[index].name);
  gLabPlayAskedMs = millis();
  return true;
}
void serviceLabPlayAsked() {
  if (!gLabPlayAsked[0]) return;
  if (labOnceHeld()) {
    if (millis() - gLabPlayAskedMs < kLabPlayWaitMs || !labOnceMayTell()) return;
    gLabPlayAsked[0] = '\0';
    labOnceSay("The Cardputer stayed busy, so the sequence didn't play.", false);
    return;
  }
  const int index = lab::findByName(gLabSeqs, gLabSeqCount, gLabPlayAsked);
  gLabPlayAsked[0] = '\0';
  if (index < 0) return;
  if (gUiMode != UiMode::LabSequences) setUiMode(UiMode::LabSequences);
  labStopPlaying();
  LabUi& ui = gLabUi;
  ui.view = LabView::Seqs;
  ui.seqRow = index;
  labKeepRowShown(ui.seqRow, ui.seqTop, kLabListRows);
  labRedraw();
  if (lab::switchesMains(gLabSeqs[index])) {
    setStatus("P plays it: it switches mains", 5000);
    return;
  }
  labSeqPlay(index, LabView::Seqs);
}
}  // namespace
