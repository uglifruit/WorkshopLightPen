#include "ComputerCard.h"

/// LIGHTPEN — a program card for the Workshop System Computer.
class LightPen : public ComputerCard
{
public:
	virtual void ProcessSample() override
	{
		// TODO: LightPen synthesis
	}
};

int main()
{
	LightPen card;
	card.Run();
}
