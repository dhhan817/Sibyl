/* ========================================================================== */
/*  Copyright (C) 2016 Hosang Yoon (hosangy@gmail.com) - All Rights Reserved  */
/*  Unauthorized copying of this file, via any medium is strictly prohibited  */
/*                        Proprietary and confidential                        */
/* ========================================================================== */

#include "Reshaper_0t.h"

#include <sibyl/Security.h>
#include <sibyl/time_common.h>
#include <sibyl/util/Config.h>

namespace sibyl
{

Reshaper_0t::Reshaper_0t(unsigned long maxGTck_,
                        TradeDataSet *pTradeDataSet_,
                        std::vector<std::string> *pFileList_,
                        const unsigned long (*ReadRawFile_)(std::vector<FLOAT>&, CSTR&, TradeDataSet*))
                        : Reshaper(maxGTck_, pTradeDataSet_, pFileList_, ReadRawFile_),
                          b_th(1.0), s_th(1.0)
{
    maxGTck   = 0;  // overwrite Reshaper's constructor value 
    inputDim  = 44;
    targetDim = 1;  // overwrite Reshaper's constructor value
}

void Reshaper_0t::ReadConfig(CSTR &filename)
{
    Config cfg(filename);
    
    auto &ss_b_th = cfg.Get("B_TH");
    ss_b_th >> b_th;
    verify(ss_b_th.fail() == false);
    
    auto &ss_s_th = cfg.Get("S_TH");
    ss_s_th >> s_th;
    verify(ss_s_th.fail() == false);
}

void Reshaper_0t::State2VecIn(FLOAT *vec, const ItemState &state)
{
    // const long interval = kTimeRates::secPerTick; // seconds
    // const long T = (const long)(std::ceil((6 * 3600 - 10 * 60)/interval) - 1);
    
    auto iItems = items.find(state.code);
    if (iItems == std::end(items))
    {
        auto it_bool = items.insert(std::make_pair(state.code, ItemMem_0t()));
        verify(it_bool.second == true);
        iItems = it_bool.first;
        iItems->second.initPr = state.tbr[idx::ps1].p;
    }
    auto &i = iItems->second; // reference to current ItemMem
    
    // store idleG
    KOSPI<Security<PQ>> sec;
    double s0f  = sec.TckLo(state.tbr[idx::ps1].p) * (1.0 - sec.dSF());
    double b0f  =           state.tbr[idx::ps1].p  * (1.0 + sec.dBF());
    double idleG = (s0f - b0f) / (s0f + b0f); // note: negative value
    verify(idleG < 0.0);
    if (1 == state.time / kTimeRates::secPerTick) i.idleG.clear();
    i.idleG.push_back(idleG);
    i.cursor = i.idleG.size() - 1; // advance time tick for VecOut2Reward
    // verify((int) i.idleG.size() == state.time / kTimeRates::secPerTick); // for debugging training
    
    unsigned long idxInput = 0;
    
    // // t
    // vec[idxInput++] = (FLOAT) state.time / (interval * T);
    
    // pr
    vec[idxInput++] = ReshapePrice(state.pr) - ReshapePrice(i.initPr);
    
    // qr
    vec[idxInput++] = ReshapeQuant((INT) state.qr);
    
    // ps1
    vec[idxInput++] = ReshapePrice(state.tbr[idx::ps1].p) - ReshapePrice(i.initPr);
    
    // ps1 - pb1
    vec[idxInput++] = ReshapePrice(state.tbr[idx::ps1].p) - ReshapePrice(state.tbr[idx::pb1].p);
    
    // // tbpr(1:20)
    // for (std::size_t idx = 0; idx < (std::size_t)idx::szTb; idx++)
    //     vec[idxInput++] = ReshapePrice(state.tbr[idx].p) - ReshapePrice(i.initPr);
    
    // tbqr(1:20)
    for (std::size_t idx = 0; idx < (std::size_t)idx::szTb; idx++)
        vec[idxInput++] = ReshapeQuant(state.tbr[idx].q);
    
    // delta_tbqr(1:20)
    for (std::size_t idx = 0; idx < (std::size_t)idx::szTb; idx++)
    {
        INT delta = state.tbr[idx].q;
        for (std::size_t idxL = 0; idxL < (std::size_t)idx::szTb; idxL++)
        {
            if (state.tbr[idx].p == i.lastTb[idxL].p)
            {
                if ( (idx <= idx::ps1 && idxL <= idx::ps1) || 
                     (idx >= idx::pb1 && idxL >= idx::pb1) )
                    delta = state.tbr[idx].q - i.lastTb[idxL].q;
                else
                    delta = state.tbr[idx].q + i.lastTb[idxL].q;
                break;
            }
        }
        vec[idxInput++] = ReshapeQuant(delta);
    }
    i.lastTb = state.tbr;
    
    verify(inputDim == idxInput);
    
    WhitenVector(vec); // this alters vector only if matrices are initialized
}

void Reshaper_0t::Reward2VecOut(FLOAT *vec, const Reward &reward, CSTR &code)
{
    auto iItems = items.find(code);
    verify(iItems != std::end(items));
    auto &i = iItems->second; // reference to current ItemMem
    
    if (vec == nullptr) // rewind idleG's cursor
    {
        i.cursor = 0;
        return;
    }
    
    verify(i.cursor < i.idleG.size());
    double idleG = i.idleG[i.cursor++]; // time tick advanced by repeated calls to this function
    
    // G' = (G - idle) / (2 * -idle) = 0.5 - G / (2 * idle)
    double G0s_scaled = 0.5 - reward.G0.s / (2.0 * idleG);
    double G0b_scaled = 0.5 - reward.G0.b / (2.0 * idleG);
    
    unsigned long idxTarget = 0;
    
    vec[idxTarget++] = (FLOAT) (G0b_scaled * (G0b_scaled > 0.0) - G0s_scaled * (G0s_scaled > 0.0));
    
    verify(targetDim == idxTarget);
}

void Reshaper_0t::VecOut2Reward(Reward &reward, const FLOAT *vec, CSTR &code)
{
    const auto iItems = items.find(code);
    verify(iItems != std::end(items));
    const auto &i = iItems->second; // reference to current ItemMem
    
    verify(i.cursor < i.idleG.size());
    double idleG = i.idleG[i.cursor]; // time tick advanced by State2VecIn
    
    unsigned long idxTarget = 0;
    
    double G_scaled = (double) vec[idxTarget++];
    
    // G = (G' - 0.5) * (2 * -idle) = (1 - 2 * G') * idle
    reward.G0.s = (s_th + 2.0 * G_scaled) * idleG;
    reward.G0.b = (b_th - 2.0 * G_scaled) * idleG;
    
    for (std::size_t j = 0; j < (std::size_t)idx::tckN; j++) reward.G[j].s  = (FLOAT)   0.0;
    for (std::size_t j = 0; j < (std::size_t)idx::tckN; j++) reward.G[j].b  = (FLOAT)   0.0;
    for (std::size_t j = 0; j < (std::size_t)idx::tckN; j++) reward.G[j].cs = (FLOAT) 100.0;
    for (std::size_t j = 0; j < (std::size_t)idx::tckN; j++) reward.G[j].cb = (FLOAT) 100.0;
    
    verify(targetDim == idxTarget);
}

}
