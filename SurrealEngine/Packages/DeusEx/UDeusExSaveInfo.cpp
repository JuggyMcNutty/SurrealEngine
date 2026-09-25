
#include "Precomp.h"
#include "UDeusExSaveInfo.h"
#include <ctime>

void UDXSaveInfo::UpdateTimeStamp()
{
	// Update the time fields
	std::time_t now = std::time(nullptr);
	std::tm* timedesc = std::localtime(&now);

	// As the original stores it: the full year, the month 1 to 12. tm counts
	// the year from 1900 and the month from 0.
	Year() = timedesc->tm_year + 1900;
	Month() = timedesc->tm_mon + 1;
	Day() = timedesc->tm_mday;
	Hour() = timedesc->tm_hour;
	Minute() = timedesc->tm_min;
	Second() = timedesc->tm_sec;
}
