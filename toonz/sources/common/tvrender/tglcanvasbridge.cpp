#include "tglcanvasbridge.h"

#include "tstencilcontrol.h"

void TGlStencilMasks::beginMask(bool drawOnce) {
  TStencilControl::instance()->beginMask(
      drawOnce ? TStencilControl::DRAW_ON_SCREEN_ONLY_ONCE
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
