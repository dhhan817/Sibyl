/* ========================================================================== */
/*  Copyright (C) 2016 Hosang Yoon (hosangy@gmail.com) - All Rights Reserved  */
/*  Unauthorized copying of this file, via any medium is strictly prohibited  */
/*                        Proprietary and confidential                        */
/* ========================================================================== */

#ifndef POLICYNET_H_
#define POLICYNET_H_

#include "../TradeNet.h"
#include "PolicyDataSet.h"

namespace fractal
{

class PolicyNet : public TradeNet<PolicyDataSet>
{
public:
    void Train() override;
private:
    void ConfigureLayers() override;
};

}

#endif /* POLICYNET_H_ */