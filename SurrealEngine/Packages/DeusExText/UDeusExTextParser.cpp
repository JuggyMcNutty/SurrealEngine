
#include "Precomp.h"
#include "UDeusExTextParser.h"
#include "Packages/Extension/UExtString.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Engine.h"
#include "Utils/Logger.h"

// The 30 tag names, in the order of EDeusExTextTags; a tag is the first one
// its content starts with, case sensitive, so <LOG ERROR> is an L. Content
// that starts with none of them has no tag (TT_None).
static const char* const DeusExTextTagNames[] =
{
	"TEXT", "FILE", "EMAIL", "NOTE", "/NOTE", "GOAL", "/GOAL", "COMMENT",
	"/COMMENT", "PLAYERNAME", "PLAYERFIRSTNAME", "NP", "JC", "JL", "JR",
	"DC", "C", "/C", "P", "B", "/B", "U", "/U", "I", "/I", "G", "F", "L",
	"/<", "/>"
};

// A tag's fields: the text after its '=', split at commas and trimmed of
// spaces. No '=' means no fields, and a missing field is empty.
static Array<std::string> ParseFields(const std::string& content)
{
	Array<std::string> fields;
	if (content.empty() || content[0] != '=')
		return fields;

	size_t pos = 1;
	while (true)
	{
		size_t comma = content.find(',', pos);
		std::string field = content.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
		size_t begin = field.find_first_not_of(' ');
		if (begin == std::string::npos)
			field.clear();
		else
			field = field.substr(begin, field.find_last_not_of(' ') - begin + 1);
		fields.push_back(std::move(field));
		if (comma == std::string::npos)
			break;
		pos = comma + 1;
	}
	return fields;
}

bool UDXTextParser::OpenText(NameString textName, std::string textPackage)
{
	CloseText();
	if (textName.IsNone())
		textName = "DeusExQuotes"; // Not correct, but it handles empty strings just fine.
	if (textPackage.empty())
		textPackage = "DeusExText";
	textObject = UObject::Cast<UDXExtString>(engine->packages->GetPackage(textPackage)->GetUObject("ExtString", textName));
	Text() = textObject ? 1 : 0;
	// The original starts a text with no paragraph yet; the last tag, name,
	// colour, file and email stay from before.
	bParagraphStarted() = false;
	return textObject;
}

void UDXTextParser::CloseText()
{
	textObject = nullptr;
	Text() = 0;
	TextPos() = 0;
	TagEndPos() = 0;
}

bool UDXTextParser::ProcessText()
{
	// One token, as the original's ParseTextBlock: a tag, or the text up to
	// the next tag with each CR and LF made a space and nothing trimmed, so
	// a line break alone between two tags is a token of two spaces. The
	// text's first <P> is swallowed and text before it dropped; false once
	// the text is used up, on the call after the last token.
	if (!textObject)
		return false;

	const std::string& text = textObject->Text();
	size_t len = text.size();
	size_t pos = (size_t)TextPos();

	while (pos < len)
	{
		if (text[pos] == '<')
		{
			DeusExTextTags tag = ParseTag(text, pos);
			if (tag == DeusExTextTags::TT_NewParagraph && !bParagraphStarted())
			{
				bParagraphStarted() = true;
				continue;
			}
			LastTag() = tag;
			TextPos() = (int)pos;
			return true;
		}

		std::string value;
		while (pos < len && text[pos] != '<')
		{
			char c = text[pos++];
			value += (c == '\r' || c == '\n') ? ' ' : c;
		}
		if (!bParagraphStarted())
			continue;
		LastTag() = DeusExTextTags::TT_Text;
		LastText() = std::move(value);
		TextPos() = (int)pos;
		return true;
	}

	TextPos() = (int)pos;
	return false;
}

DeusExTextTags UDXTextParser::ParseTag(const std::string& text, size_t& pos)
{
	// A tag runs from '<' to the first '>' (the original's ParseTag), and
	// its effects are the original's ParseToken.
	size_t start = pos + 1;
	size_t close = text.find('>', start);
	std::string content = close == std::string::npos ? text.substr(start) : text.substr(start, close - start);
	pos = close == std::string::npos ? text.size() : close + 1;

	DeusExTextTags tag = DeusExTextTags::TT_None;
	std::string fields;
	for (int i = 0; i < 30; i++)
	{
		size_t nameLen = strlen(DeusExTextTagNames[i]);
		if (content.compare(0, nameLen, DeusExTextTagNames[i]) == 0)
		{
			tag = (DeusExTextTags)i;
			fields = content.substr(nameLen);
			break;
		}
	}

	switch (tag)
	{
	case DeusExTextTags::TT_File:
		ParseFile(fields);
		break;
	case DeusExTextTags::TT_Email:
		ParseEmail(fields);
		break;
	case DeusExTextTags::TT_Note:
		return FindEndTag(text, pos, DeusExTextTags::TT_EndNote);
	case DeusExTextTags::TT_Goal:
		return FindEndTag(text, pos, DeusExTextTags::TT_EndGoal);
	case DeusExTextTags::TT_Comment:
		return FindEndTag(text, pos, DeusExTextTags::TT_EndComment);
	case DeusExTextTags::TT_PlayerName:
		LastText() = PlayerName();
		break;
	case DeusExTextTags::TT_PlayerFirstName:
		LastText() = PlayerFirstName();
		break;
	case DeusExTextTags::TT_DefaultColor:
		DefaultColor() = ParseColor(fields);
		LastColor() = DefaultColor();
		break;
	case DeusExTextTags::TT_TextColor:
		LastColor() = ParseColor(fields);
		break;
	case DeusExTextTags::TT_Graphic:
	case DeusExTextTags::TT_Font:
		if (!fields.empty() && fields[0] == '=')
			LastName() = NameString(fields.substr(1));
		break;
	case DeusExTextTags::TT_OpenBracket:
		LastText() = "<";
		break;
	case DeusExTextTags::TT_CloseBracket:
		LastText() = ">";
		break;
	default:
		break;
	}
	return tag;
}

DeusExTextTags UDXTextParser::FindEndTag(const std::string& text, size_t& pos, DeusExTextTags endTag)
{
	// The original's FindEndTag: NOTE, GOAL and COMMENT hide what they hold
	// and the token's tag is the end tag's. The tags inside are read and the
	// character after each is skipped, so an end tag straight after another
	// tag is missed. With no end tag the rest of the text is hidden -- the
	// original reads on past the text's end there; this stops at it.
	size_t len = text.size();
	while (pos < len)
	{
		if (text[pos] != '<')
		{
			pos++;
			continue;
		}

		size_t start = pos + 1;
		size_t close = text.find('>', start);
		std::string content = close == std::string::npos ? text.substr(start) : text.substr(start, close - start);
		pos = close == std::string::npos ? len : close + 1;

		DeusExTextTags tag = DeusExTextTags::TT_None;
		for (int i = 0; i < 30; i++)
		{
			if (content.compare(0, strlen(DeusExTextTagNames[i]), DeusExTextTagNames[i]) == 0)
			{
				tag = (DeusExTextTags)i;
				break;
			}
		}
		if (tag == endTag)
			return endTag;
		pos++;
	}
	return endTag;
}

void UDXTextParser::ParseFile(const std::string& fields)
{
	// FILE=name,description; the last ones read stay, whatever the token.
	Array<std::string> f = ParseFields(fields);
	LastFileName() = f.size() > 0 ? f[0] : "";
	LastFileDescription() = f.size() > 1 ? f[1] : "";
}

void UDXTextParser::ParseEmail(const std::string& fields)
{
	// EMAIL=name,subject,from,to,cc; the last ones read stay, whatever the
	// token.
	Array<std::string> f = ParseFields(fields);
	LastEmailName() = f.size() > 0 ? f[0] : "";
	LastEmailSubject() = f.size() > 1 ? f[1] : "";
	LastEmailFrom() = f.size() > 2 ? f[2] : "";
	LastEmailTo() = f.size() > 3 ? f[3] : "";
	LastEmailCC() = f.size() > 4 ? f[4] : "";
}

Color UDXTextParser::ParseColor(const std::string& fields)
{
	// The three numbers after '=', or black without one. The original's
	// alpha is whatever byte was left there; ours is opaque.
	Array<std::string> f = ParseFields(fields);
	Color color;
	color.R = f.size() > 0 ? (uint8_t)std::atoi(f[0].c_str()) : 0;
	color.G = f.size() > 1 ? (uint8_t)std::atoi(f[1].c_str()) : 0;
	color.B = f.size() > 2 ? (uint8_t)std::atoi(f[2].c_str()) : 0;
	color.A = 255;
	return color;
}

bool UDXTextParser::IsEOF()
{
	return !textObject || (size_t)TextPos() >= textObject->Text().size();
}

std::string UDXTextParser::GetText()
{
	return LastText();
}

void UDXTextParser::GotoLabel(const std::string& label)
{
	// The original's does nothing.
}

uint8_t UDXTextParser::GetTag()
{
	return static_cast<uint8_t>(LastTag());
}

NameString UDXTextParser::GetName()
{
	return LastName();
}

Color UDXTextParser::GetColor()
{
	// After /C, the colour the parser was made with: black, as nothing
	// sets it.
	if (LastTag() == DeusExTextTags::TT_RevertColor)
	{
		Color black;
		black.R = 0;
		black.G = 0;
		black.B = 0;
		black.A = 255;
		return black;
	}
	return LastColor();
}

void UDXTextParser::GetEmailInfo(std::string& name, std::string& subject, std::string& from, std::string& to, std::string& cc)
{
	// The last ones read, whatever the token, as the original's.
	name = LastEmailName();
	subject = LastEmailSubject();
	from = LastEmailFrom();
	to = LastEmailTo();
	cc = LastEmailCC();
}

void UDXTextParser::GetFileInfo(std::string& fileName, std::string& fileDescription)
{
	fileName = LastFileName();
	fileDescription = LastFileDescription();
}

void UDXTextParser::SetPlayerName(const std::string& newPlayerName)
{
	PlayerName() = newPlayerName;
	PlayerFirstName() = newPlayerName.substr(0, newPlayerName.find(' '));
}
