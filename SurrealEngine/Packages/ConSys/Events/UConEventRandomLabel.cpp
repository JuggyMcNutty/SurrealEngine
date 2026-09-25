
#include "Precomp.h"
#include "UConEventRandomLabel.h"

void UConEventRandomLabel::Load(ObjectStream* stream)
{
	UConEvent::Load(stream);
	int count = stream->ReadIndex();
	for (int i = 0; i < count; i++)
	{
		std::string label = stream->ReadString();
		labels.push_back(std::move(label));
	}
}

std::string UConEventRandomLabel::GetLabel(int labelIndex)
{
	std::string str = labels[labelIndex];
	return str;
}

int UConEventRandomLabel::GetLabelCount()
{
	return (int)labels.size();
}

std::string UConEventRandomLabel::GetRandomLabel()
{
	// The original's cycling: random when not cycling; otherwise the labels
	// in turn and then, once every label has had its turn, the last forever
	// (bCycleOnce), random forever (bCycleRandom), or around again.
	int count = (int)labels.size();
	if (count <= 0)
		return {};

	if (!bCycleEvents())
		return labels[std::rand() % count];

	if (cycleIndex() >= count)
	{
		bLabelsCycled() = true;
		if (bCycleRandom())
			return labels[std::rand() % count];
		if (bCycleOnce())
			return labels[count - 1];
		cycleIndex() = 0;
	}

	return labels[cycleIndex()++];
}
