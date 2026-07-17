#include <ParameterInfo.h>

namespace mrta
{

// Float ctor
ParameterInfo::ParameterInfo(const juce::String& _ID, const juce::String& _name, const juce::String& _unit,
                float _def, float _min, float _max,
                float _inc, float _skw) :
    ID { _ID }, name { _name }, unit { _unit }, steps { },
    type { Float },
    def { _def }, min { _min }, max { _max },
    inc { _inc }, skw { _skw }
{
    jassert(ID.isNotEmpty());
    jassert(min < max);
    jassert(def <= max && def >= min);
    jassert(inc > 0.f);
    jassert(skw > 0.f);
}

// Choice ctor
ParameterInfo::ParameterInfo( const juce::String& _ID, const juce::String& _name, const juce::StringArray& _steps, unsigned int _def) :
    ID { _ID }, name { _name }, unit { }, steps { _steps },
    type { Choice },
    def { static_cast<float>(_def) }, min { 0.f }, max { static_cast<float>(_steps.size() - 1) },
    inc { 1.f }, skw { 1.f }
{
    jassert(ID.isNotEmpty());
    jassert(min < max);
    jassert(def <= max && def >= min);
    jassert(steps.size() == 0 || steps.size() == static_cast<int>(max + 1.f));
}

// Bool ctor
ParameterInfo::ParameterInfo(const juce::String& _ID, const juce::String& _name,
                const juce::String& offStepName, const juce::String& onStepName,
                bool _def) :
    ID { _ID }, name { _name }, unit { }, steps { offStepName, onStepName },
    type { Bool },
    def { static_cast<float>(_def) }, min { 0.f }, max { 1.f },
    inc { 1.f }, skw { 1.f }
{
    jassert(ID.isNotEmpty());
}

// Generic ctor
ParameterInfo::ParameterInfo(const juce::String& _ID, const juce::String& _name, const juce::String& _unit, const juce::String& _steps,
                Type _type,
                float _def, float _min, float _max,
                float _inc, float _skw) :
    ID { _ID }, name { _name }, unit { _unit }, steps { _steps },
    type { _type },
    def { _def }, min { _min }, max { _max },
    inc { _inc }, skw { _skw }
{
    jassert(ID.isNotEmpty());
    jassert(min < max);
    jassert(def <= max && def >= min);
    jassert(inc > 0.f);
    jassert(skw > 0.f);
}

// Copy ctor
ParameterInfo::ParameterInfo(const ParameterInfo& other) :
    ID { other.ID }, name { other.name }, unit { other.unit }, steps { other.steps },
    type { other.type },
    def { other.def }, min { other.min }, max { other.max },
    inc { other.inc }, skw { other.skw }
{ }

// Move ctor
ParameterInfo::ParameterInfo(ParameterInfo&& other) :
    ID { other.ID }, name { other.name },
    unit { other.unit }, steps { other.steps },
    type { other.type },
    def { other.def }, min { other.min }, max { other.max },
    inc { other.inc }, skw { other.skw }
{ }

}
