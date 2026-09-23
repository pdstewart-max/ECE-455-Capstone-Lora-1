#pragma once
#include "mesh/SinglePortModule.h"
#include "concurrency/OSThread.h"

class PocTimeModule : public SinglePortModule, private concurrency::OSThread {
  public:
    PocTimeModule();
    virtual int32_t runOnce() override;

  protected:
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }
};

extern PocTimeModule *pocTimeModule;