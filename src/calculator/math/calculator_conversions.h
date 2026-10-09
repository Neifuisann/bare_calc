#pragma once
namespace Calculator {
struct ConversionEntry {const char* group;const char* label;const char* factor;const char* offset;bool inverse;};
// NIST SP 811 (2008) factors. Each reverse conversion reuses the same factor,
// avoiding asymmetry from independently rounded reciprocals.
#define CONVERSION_PAIR(G,A,B,F) {G,A " -> " B,F,"0",false},{G,B " -> " A,F,"0",true}
inline constexpr ConversionEntry kConversions[]={
 CONVERSION_PAIR("Length","in","cm","2.54"),
 CONVERSION_PAIR("Length","ft","m","0.3048"),
 CONVERSION_PAIR("Length","yd","m","0.9144"),
 CONVERSION_PAIR("Length","mile","km","1.609344"),
 CONVERSION_PAIR("Length","n mile","m","1852"),
 CONVERSION_PAIR("Length","pc","km","30856775814913.673"),
 CONVERSION_PAIR("Area","acre","m2","4046.8564224"),
 CONVERSION_PAIR("Volume","gal(US)","L","3.785411784"),
 CONVERSION_PAIR("Volume","gal(UK)","L","4.54609"),
 CONVERSION_PAIR("Mass","oz","g","28.349523125"),
 CONVERSION_PAIR("Mass","lb","kg","0.45359237"),
 {"Velocity","km/h -> m/s","3.6","0",true},{"Velocity","m/s -> km/h","3.6","0",false},
 CONVERSION_PAIR("Pressure","atm","Pa","101325"),
 CONVERSION_PAIR("Pressure","mmHg","Pa","133.3224"),
 CONVERSION_PAIR("Pressure","kgf/cm2","Pa","98066.5"),
 CONVERSION_PAIR("Pressure","lbf/in2","kPa","6.894757293168361"),
 CONVERSION_PAIR("Energy","kgf m","J","9.80665"),
 {"Energy","J -> cal15","4.1858","0",true},{"Energy","cal15 -> J","4.1858","0",false},
 CONVERSION_PAIR("Power","hp","kW","0.7456998715822702"),
 {"Temperature","F -> C","1.8","32",true},{"Temperature","C -> F","1.8","32",false},
};
#undef CONVERSION_PAIR
}
