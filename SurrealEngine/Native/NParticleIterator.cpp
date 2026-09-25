
#include "Precomp.h"
#include "NParticleIterator.h"
#include "VM/NativeFunc.h"
#include "Packages/DeusEx/UParticleIterator.h"

void NParticleIterator::RegisterFunctions()
{
	RegisterVMNativeFunc_1("ParticleIterator", "UpdateParticles", &NParticleIterator::UpdateParticles, 3017);
}

void NParticleIterator::UpdateParticles(UObject* Self, float DeltaTime)
{
	UObject::Cast<UParticleIterator>(Self)->UpdateParticles(DeltaTime);
}
