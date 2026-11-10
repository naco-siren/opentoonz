#include "tglcanvasbridge.h"

#include "tstencilcontrol.h"

void TGlStencilMasks::beginMask(Screen screen) {
  TStencilControl::instance()->beginMask(
      screen == Screen::DrawnOnce
          ? TStencilControl::DRAW_ON_SCREEN_ONLY_ONCE
          : screen == Screen::DrawnToo ? TStencilControl::DRAW_ALSO_ON_SCREEN
                                       : TStencilControl::DRAW_ONLY_MASK);
}

void TGlStencilMasks::endMask() { TStencilControl::instance()->endMask(); }

void TGlStencilMasks::enableMask(bool inside) {
  TStencilControl::instance()->enableMask(inside
                                              ? TStencilControl::SHOW_INSIDE
                                              : TStencilControl::SHOW_OUTSIDE);
}

void TGlStencilMasks::disableMask() {
  TStencilControl::instance()->disableMask();
}

//-----------------------------------------------------------------------------

TLegacyGlCanvas::TLegacyGlCanvas()
    : m_canvas(TCanvasIRect(), m_masks, TGlCanvas::State::Callers) {}

TLegacyGlCanvas::~TLegacyGlCanvas() {}
