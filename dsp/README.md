# OpenEQ DSP engineering

The beta engine uses continuous-order filter designs, analogue sections, fitted response cascades, per-band dynamics, and FIR processing. Implementation details are in `Source/FilterDesign.h`, `Source/VintageFilters.h`, `Source/ResponseModel.h`, `Source/DynamicsEngine.h`, and `Source/LinearPhaseEngine.cpp`.

Current numerical checks are in `Tests/FilterResponseTests.cpp` and `Tests/RegressionTests.cpp`. Historical comparison research is excluded from the public source distribution. A passing regression suite does not establish complete host, routing, or listening validation.
