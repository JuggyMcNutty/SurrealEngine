
#include "Precomp.h"
#include "UListWindow.h"
#include "Engine.h"
#include "GameWindow.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Extension/Windows/UGC.h"
#include <algorithm>

// COLTYPE_String, COLTYPE_Float, COLTYPE_Time
enum { ColTypeString = 0, ColTypeFloat = 1, ColTypeTime = 2 };

// MOVELIST_Up .. MOVELIST_End
enum { MoveListUp = 0, MoveListDown = 1, MoveListPageUp = 2, MoveListPageDown = 3, MoveListHome = 4, MoveListEnd = 5 };

static int CompareText(const std::string& a, const std::string& b, bool caseSensitive)
{
	if (caseSensitive)
	{
		int cmp = a.compare(b);
		return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
	}
	size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < n; i++)
	{
		int ca = std::tolower((unsigned char)a[i]);
		int cb = std::tolower((unsigned char)b[i]);
		if (ca != cb)
			return ca < cb ? -1 : 1;
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

void UListWindow::InitWindow()
{
	focusLine() = -1;
	anchorLine() = -1;
	lastIndex() = -1;
	// The original's defaults: the delimiter ';'; auto sort off,
	// auto-expanding columns, multiple selection and hot keys (column 0) on;
	// a column margin of 3 and a row margin of 1; one column.
	Delimiter() = ";";
	bMultiSelect() = true;
	bAutoExpandColumns() = true;
	bHotKeys() = true;
	hotKeyCol() = 0;
	colMargin() = 3.0f;
	rowMargin() = 1.0f;
	focusThickness() = 1.0f;
	SetNumColumns(1);
	UWindow::InitWindow();
}

void UListWindow::SplitRow(Item& item, const std::string& rowStr)
{
	item.cells.clear();
	const std::string& delim = Delimiter();
	if (delim.empty())
	{
		item.cells.push_back({ rowStr, 0.0f });
	}
	else
	{
		size_t start = 0;
		while (true)
		{
			size_t pos = rowStr.find(delim, start);
			item.cells.push_back({ rowStr.substr(start, pos == std::string::npos ? std::string::npos : pos - start), 0.0f });
			if (pos == std::string::npos)
				break;
			start = pos + delim.size();
		}
	}
	for (int colIndex = 0; colIndex < (int)item.cells.size(); colIndex++)
	{
		UpdateCellValue(item, colIndex);
		AutoExpandColumn(colIndex, FieldDisplayText(item, colIndex));
	}
}

void UListWindow::UpdateCellValue(Item& item, int colIndex)
{
	// A float or time field keeps the number read from the text; a string
	// field's number is 0.
	if (colIndex < 0 || (size_t)colIndex >= item.cells.size())
		return;
	uint8_t type = (size_t)colIndex < columns.size() ? columns[colIndex].type : (uint8_t)ColTypeString;
	item.cells[colIndex].value = (type == ColTypeFloat || type == ColTypeTime) ? StringToFloat(item.cells[colIndex].text) : 0.0f;
}

float UListWindow::StringToFloat(const std::string& text)
{
	// The original's: a sign; octal after a leading 0 and hex after 0x; a
	// decimal point or comma; ' adds the number so far as hours, : or " as
	// minutes; anything else ends it.
	size_t pos = 0;
	size_t len = text.size();
	double sign = 1.0;
	if (pos < len && (text[pos] == '-' || text[pos] == '+'))
	{
		if (text[pos] == '-')
			sign = -1.0;
		pos++;
	}

	double total = 0.0;
	double value = 0.0;
	while (true)
	{
		value = 0.0;
		int base = 10;
		if (pos < len && text[pos] == '0')
		{
			if (pos + 1 < len && (text[pos + 1] == 'x' || text[pos + 1] == 'X'))
			{
				base = 16;
				pos += 2;
			}
			else
			{
				base = 8;
			}
		}
		while (pos < len)
		{
			char c = text[pos];
			int digit;
			if (c >= '0' && c <= '9')
				digit = c - '0';
			else if (base == 16 && c >= 'a' && c <= 'f')
				digit = c - 'a' + 10;
			else if (base == 16 && c >= 'A' && c <= 'F')
				digit = c - 'A' + 10;
			else
				break;
			if (digit >= base)
				break;
			value = value * base + digit;
			pos++;
		}
		if (pos < len && (text[pos] == '.' || text[pos] == ','))
		{
			pos++;
			double fraction = 1.0;
			while (pos < len && text[pos] >= '0' && text[pos] <= '9')
			{
				fraction /= 10.0;
				value += (text[pos] - '0') * fraction;
				pos++;
			}
		}
		if (pos < len && text[pos] == '\'')
		{
			total += value * 3600.0;
			pos++;
			continue;
		}
		if (pos < len && (text[pos] == ':' || text[pos] == '"'))
		{
			total += value * 60.0;
			pos++;
			continue;
		}
		break;
	}
	return (float)(sign * (total + value));
}

std::string UListWindow::FieldDisplayText(const Item& item, int colIndex)
{
	// A float field shows its number through the column's format, %f by
	// default; a time field shows as %f too, its own format unused.
	if (colIndex < 0 || (size_t)colIndex >= item.cells.size())
		return {};
	const Cell& cell = item.cells[colIndex];
	uint8_t type = (size_t)colIndex < columns.size() ? columns[colIndex].type : (uint8_t)ColTypeString;
	if (type == ColTypeFloat || type == ColTypeTime)
	{
		const char* format = "%f";
		if (type == ColTypeFloat && columns[colIndex].format)
			format = columns[colIndex].format->c_str();
		char buffer[64];
		snprintf(buffer, sizeof(buffer), format, cell.value);
		return buffer;
	}
	return cell.text;
}

float UListWindow::MeasureText(UFont* colFont, const std::string& text)
{
	UFont* font = colFont ? colFont : normalFont();
	if (!font)
		return 0.0f;
	float x = 0.0f;
	for (char c : text)
		x += (float)font->GetGlyph(c).USize;
	return x;
}

float UListWindow::GetLineHeight()
{
	UFont* font = normalFont();
	if (!font)
		return lineSize() > 0.0f ? lineSize() : 0.0f;
	return (float)font->GetGlyph('X').VSize + rowMargin() * 2.0f;
}

void UListWindow::AutoExpandColumn(int colIndex, const std::string& displayText)
{
	// With auto-expanding columns, each field set widens its column to the
	// field's text plus both margins.
	if (!bAutoExpandColumns())
		return;
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	Column& col = columns[colIndex];
	col.width = std::max(col.width, MeasureText(col.font, displayText) + colMargin() * 2.0f);
}

int UListWindow::AddRow(const std::string& rowStr, std::optional<int> clientData)
{
	int id = nextRowId++;
	Item item;
	item.id = id;
	if (clientData.has_value())
		item.clientInt = clientData.value();
	items.push_back(std::move(item));
	SplitRow(items.back(), rowStr);
	// With auto sort on, a new row goes in at its place.
	if (bAutoSort())
		Sort();
	return id;
}

void UListWindow::AddSortColumn(int colIndex, std::optional<bool> bReverse, std::optional<bool> bCaseSensitive)
{
	// Adds the column as the last key.
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	sortColumns.erase(std::remove(sortColumns.begin(), sortColumns.end(), colIndex), sortColumns.end());
	sortColumns.push_back(colIndex);
	columns[colIndex].sortReverse = bReverse.value_or(false);
	columns[colIndex].sortCaseSensitive = bCaseSensitive.value_or(false);
	SortChanged();
}

void UListWindow::DeleteAllRows()
{
	items.clear();
	nextRowId = 1;
	focusLine() = -1;
	anchorLine() = -1;
}

void UListWindow::DeleteRow(int rowId)
{
	int index = RowIdToIndex(rowId);
	if (index < 0)
		return;
	items.erase(items.begin() + index);
	if (focusLine() == index)
		focusLine() = -1;
	else if (focusLine() > index)
		focusLine()--;
	if (anchorLine() == index)
		anchorLine() = -1;
	else if (anchorLine() > index)
		anchorLine()--;
}

void UListWindow::EnableAutoExpandColumns(std::optional<bool> bAutoExpand)
{
	bAutoExpandColumns() = bAutoExpand.has_value() ? bAutoExpand.value() : true;
	// Turning it on widens every column.
	if (bAutoExpandColumns())
		ResizeColumns(true);
}

void UListWindow::EnableAutoSort(std::optional<bool> bNewAutoSort)
{
	bAutoSort() = bNewAutoSort.has_value() ? bNewAutoSort.value() : true;
	if (bAutoSort())
		Sort();
}

void UListWindow::EnableHotKeys(std::optional<bool> bEnable)
{
	bHotKeys() = bEnable.has_value() ? bEnable.value() : true;
}

void UListWindow::EnableMultiSelect(std::optional<bool> bEnableMultiSelect)
{
	bMultiSelect() = bEnableMultiSelect.has_value() ? bEnableMultiSelect.value() : true;
}

uint8_t UListWindow::GetColumnAlignment(int colIndex)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return 0;
	return (uint8_t)columns[colIndex].align;
}

void UListWindow::GetColumnColor(int colIndex, Color& colColor)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		colColor = { 255, 255, 255, 255 };
	else
		colColor = columns[colIndex].color;
}

UObject* UListWindow::GetColumnFont(int colIndex)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return nullptr;
	return columns[colIndex].font;
}

std::string UListWindow::GetColumnTitle(int colIndex)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return {};
	return columns[colIndex].title;
}

uint8_t UListWindow::GetColumnType(int colIndex)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return 0;
	return columns[colIndex].type;
}

float UListWindow::GetColumnWidth(int colIndex)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return 0;
	return columns[colIndex].width;
}

std::string UListWindow::GetField(int rowId, int colIndex)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return {};
	if (colIndex < 0 || (size_t)colIndex >= items[rowIndex].cells.size())
		return {};
	return items[rowIndex].cells[colIndex].text;
}

void UListWindow::GetFieldMargins(float& marginWidth, float& marginHeight)
{
	marginWidth = colMargin();
	marginHeight = rowMargin();
}

float UListWindow::GetFieldValue(int rowId, int colIndex)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return 0.0f;
	if (colIndex < 0 || (size_t)colIndex >= items[rowIndex].cells.size())
		return 0.0f;
	return items[rowIndex].cells[colIndex].value;
}

int UListWindow::GetFocusRow()
{
	if (focusLine() < 0 || (size_t)focusLine() >= items.size())
		return 0;
	return items[focusLine()].id;
}

int UListWindow::GetNumColumns()
{
	return (int)columns.size();
}

int UListWindow::GetNumRows()
{
	return (int)items.size();
}

int UListWindow::GetNumSelectedRows()
{
	int count = 0;
	for (auto& item : items)
	{
		if (item.selected)
			count++;
	}
	return count;
}

int UListWindow::GetPageSize()
{
	// The rows that fit the list's clipped height, at least 1. The list
	// lives in a clip window, whose height is the visible part.
	float lineHeight = GetLineHeight();
	if (lineHeight <= 0.0f)
		return 1;
	UWindow* parent = parentOwner();
	float clippedHeight = parent ? parent->Height() : Height();
	return std::max(1, (int)(clippedHeight / lineHeight));
}

int UListWindow::GetRowClientInt(int rowId)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return 0;
	return items[rowIndex].clientInt;
}

UObject* UListWindow::GetRowClientObject(int rowId)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return 0;
	return items[rowIndex].clientObj;
}

int UListWindow::GetSelectedRow()
{
	// The focus row if it is selected, else the first selected row.
	if (focusLine() >= 0 && (size_t)focusLine() < items.size() && items[focusLine()].selected)
		return items[focusLine()].id;
	for (auto& item : items)
	{
		if (item.selected)
		{
			return item.id;
		}
	}
	return 0;
}

void UListWindow::HideColumn(int colIndex, std::optional<bool> bHide)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	columns[colIndex].hidden = bHide.has_value() ? bHide.value() : true;
}

int UListWindow::IndexToRowId(int index)
{
	if (index < 0 || (size_t)index >= items.size())
		return -1;
	return items[index].id;
}

bool UListWindow::IsAutoExpandColumnsEnabled()
{
	return bAutoExpandColumns();
}

bool UListWindow::IsAutoSortEnabled()
{
	return bAutoSort();
}

bool UListWindow::IsColumnHidden(int colIndex)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return false;
	return columns[colIndex].hidden;
}

bool UListWindow::IsMultiSelectEnabled()
{
	return bMultiSelect();
}

bool UListWindow::IsRowSelected(int rowId)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return false;
	return items[rowIndex].selected;
}

void UListWindow::ModifyRow(int rowId, const std::string& rowStr)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return;
	SplitRow(items[rowIndex], rowStr);
	// With auto sort on, a changed row is moved to its place.
	if (bAutoSort())
		Sort();
}

void UListWindow::MoveRow(uint8_t Move, std::optional<bool> bSelect, std::optional<bool> bClearRows, std::optional<bool> bDrag)
{
	// Moves the focus row, clamped to the rows, starting from the first row
	// when there is no focus. The list's script sends the arrow keys, Page
	// Up and Down, Home and End here.
	if (items.empty())
		return;

	int last = (int)items.size() - 1;
	int cur = (focusLine() >= 0 && focusLine() <= last) ? focusLine() : 0;
	int page = GetPageSize();
	int index = cur;
	switch (Move)
	{
	case MoveListUp: index = cur - 1; break;
	case MoveListDown: index = cur + 1; break;
	case MoveListPageUp: index = cur - page; break;
	case MoveListPageDown: index = cur + page; break;
	case MoveListHome: index = 0; break;
	case MoveListEnd: index = last; break;
	default: return;
	}
	index = std::clamp(index, 0, last);

	MoveToRow(index, bSelect.value_or(true), bClearRows.value_or(true), bDrag.value_or(false));
}

void UListWindow::MoveToRow(int index, bool bSelect, bool bClearRows, bool bDrag)
{
	if (index < 0 || (size_t)index >= items.size())
		return;

	bool newFocus = (index != focusLine());
	if (bClearRows)
	{
		for (auto& item : items)
			item.selected = false;
	}
	if (bDrag && anchorLine() >= 0 && (size_t)anchorLine() < items.size())
	{
		// Extend the selection from the anchor.
		int from = std::min(anchorLine(), index);
		int to = std::max(anchorLine(), index);
		for (int i = from; i <= to; i++)
			items[i].selected = true;
	}
	else if (bSelect)
	{
		items[index].selected = true;
		anchorLine() = index;
	}
	focusLine() = index;

	// A new focus row plays the move sound and is scrolled into view.
	if (newFocus)
	{
		if (moveSound())
			PlaySound(moveSound(), {}, {}, {}, {});
		ShowFocusRow();
	}
	DispatchListSelectionChanged();
}

void UListWindow::PlayListSound(UObject* listSound, std::optional<float> Volume, std::optional<float> Pitch)
{
	if (listSound)
		PlaySound(listSound, Volume, Pitch, {}, {});
}

void UListWindow::RemoveSortColumn(int colIndex)
{
	sortColumns.erase(std::remove(sortColumns.begin(), sortColumns.end(), colIndex), sortColumns.end());
	SortChanged();
}

void UListWindow::ResetSortColumns(std::optional<bool> bSort)
{
	// True, the default: every column a key, in column order; false, none.
	// Reverse and case are cleared.
	sortColumns.clear();
	for (size_t i = 0; i < columns.size(); i++)
	{
		columns[i].sortReverse = false;
		columns[i].sortCaseSensitive = false;
		if (bSort.value_or(true))
			sortColumns.push_back((int)i);
	}
	SortChanged();
}

void UListWindow::ResizeColumns(std::optional<bool> bExpandOnly)
{
	// Widens every column to its fields plus both margins; without
	// bExpandOnly every column first shrinks to its margins.
	for (int colIndex = 0; colIndex < (int)columns.size(); colIndex++)
	{
		Column& col = columns[colIndex];
		if (!bExpandOnly.value_or(true))
			col.width = colMargin() * 2.0f;
		for (auto& item : items)
			col.width = std::max(col.width, MeasureText(col.font, FieldDisplayText(item, colIndex)) + colMargin() * 2.0f);
	}
}

int UListWindow::RowIdToIndex(int rowId)
{
	int index = 0;
	for (auto& item : items)
	{
		if (item.id == rowId)
		{
			return index;
		}
		index++;
	}
	return -1;
}

void UListWindow::SelectAllRows(std::optional<bool> bSelect)
{
	bool selected = bSelect.has_value() ? bSelect.value() : true;
	for (auto& item : items)
	{
		item.selected = selected;
	}
}

void UListWindow::SelectRow(int rowId, std::optional<bool> bSelect)
{
	bool selected = bSelect.has_value() ? bSelect.value() : true;
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex != -1)
		items[rowIndex].selected = selected;
}

void UListWindow::SelectToRow(int rowId, std::optional<bool> bClearRows, std::optional<bool> bInvert, std::optional<bool> bSpanRows)
{
	// UNUSED from scripts.
	LogUnimplemented("ListWindow.SelectToRow");
}

void UListWindow::SetColumnAlignment(int colIndex, uint8_t newAlign)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	columns[colIndex].align = (EHAlign)newAlign;
}

void UListWindow::SetColumnColor(int colIndex, const Color& NewColor)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	columns[colIndex].color = NewColor;
}

void UListWindow::SetColumnFont(int colIndex, UObject* NewFont)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	columns[colIndex].font = UObject::Cast<UFont>(NewFont);
}

void UListWindow::SetColumnTitle(int colIndex, const std::string& Title)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	columns[colIndex].title = Title;
}

void UListWindow::SetColumnType(int colIndex, uint8_t newType, std::optional<std::string> newFmt)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	columns[colIndex].type = newType;
	columns[colIndex].format = newFmt;
	for (auto& item : items)
		UpdateCellValue(item, colIndex);
	if (bAutoSort())
		Sort();
}

void UListWindow::SetColumnWidth(int colIndex, float newWidth)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	columns[colIndex].width = newWidth;
}

void UListWindow::SetDelimiter(const std::string& newDelimiter)
{
	Delimiter() = newDelimiter;
}

void UListWindow::SetField(int rowId, int colIndex, const std::string& fieldStr)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return;
	Item& item = items[rowIndex];
	if (item.cells.size() <= (size_t)colIndex)
		item.cells.resize(colIndex + 1);
	item.cells[colIndex].text = fieldStr;
	UpdateCellValue(item, colIndex);
	AutoExpandColumn(colIndex, FieldDisplayText(item, colIndex));
	if (bAutoSort())
		Sort();
}

void UListWindow::SetFieldMargins(float newMarginWidth, float newMarginHeight)
{
	colMargin() = newMarginWidth;
	rowMargin() = newMarginHeight;
}

void UListWindow::SetFieldValue(int rowId, int colIndex, float NewValue)
{
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return;
	Item& item = items[rowIndex];
	if (item.cells.size() <= (size_t)colIndex)
		item.cells.resize(colIndex + 1);
	item.cells[colIndex].value = NewValue;
	item.cells[colIndex].text = FieldDisplayText(item, colIndex);
	AutoExpandColumn(colIndex, item.cells[colIndex].text);
	if (bAutoSort())
		Sort();
}

void UListWindow::SetFocusColor(const Color& NewColor)
{
	focusColor() = NewColor;
}

void UListWindow::SetFocusRow(int rowId, std::optional<bool> bMoveTo, std::optional<bool> bAnchor)
{
	focusLine() = RowIdToIndex(rowId);
	if (bAnchor.value_or(false))
		anchorLine() = focusLine();
	if (bMoveTo.value_or(false))
		ShowFocusRow();
}

void UListWindow::SetFocusTexture(UObject* NewTexture)
{
	focusTexture() = UObject::Cast<UTexture>(NewTexture);
}

void UListWindow::SetFocusThickness(float newThickness)
{
	focusThickness() = newThickness;
}

void UListWindow::SetHighlightColor(const Color& NewColor)
{
	highlightColor() = NewColor;
}

void UListWindow::SetHighlightTextColor(const Color& NewColor)
{
	highlightTextColor = NewColor;
}

void UListWindow::SetHighlightTexture(UObject* NewTexture)
{
	highlightTexture() = UObject::Cast<UTexture>(NewTexture);
}

void UListWindow::SetHotKeyColumn(int colIndex)
{
	hotKeyCol() = colIndex;
}

void UListWindow::SetListSounds(std::optional<UObject*> newActivateSound, std::optional<UObject*> newMoveSound)
{
	if (newActivateSound.has_value())
		ActivateSound() = UObject::Cast<USound>(newActivateSound.value());
	if (newMoveSound.has_value())
		moveSound() = UObject::Cast<USound>(newMoveSound.value());
}

void UListWindow::SetNumColumns(int newCols)
{
	if (newCols < 0)
		newCols = 0;
	size_t oldCount = columns.size();
	columns.resize(newCols);
	sortColumns.erase(std::remove_if(sortColumns.begin(), sortColumns.end(), [&](int c) { return c >= newCols; }), sortColumns.end());
	for (size_t i = oldCount; i < columns.size(); i++)
	{
		// A new column: 20 wide plus both margins, left-aligned, the
		// window's text colour and font, string, and a sort key.
		Column& col = columns[i];
		col.width = 20.0f + colMargin() * 2.0f;
		col.align = EHAlign::Left;
		col.color = TextColor();
		col.font = normalFont();
		col.type = ColTypeString;
		sortColumns.push_back((int)i);
	}
}

void UListWindow::SetRow(int rowId, std::optional<bool> bSelect, std::optional<bool> bClearRows, std::optional<bool> bDrag)
{
	if (!bClearRows.has_value() || *bClearRows)
		SelectAllRows(false);
	if (!bSelect.has_value() || *bSelect)
		SelectRow(rowId, true);
}

void UListWindow::SetRowClientInt(int rowId, int clientInt)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return;
	items[rowIndex].clientInt = clientInt;
}

void UListWindow::SetRowClientObject(int rowId, UObject* clientObj)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return;
	items[rowIndex].clientObj = clientObj;
}

void UListWindow::SetSortColumn(int colIndex, std::optional<bool> bReverse, std::optional<bool> bCaseSensitive)
{
	// That column first, then every other column in column order, their
	// reverse and case flags cleared.
	if (colIndex < 0 || (size_t)colIndex >= columns.size())
		return;
	for (auto& col : columns)
	{
		col.sortReverse = false;
		col.sortCaseSensitive = false;
	}
	columns[colIndex].sortReverse = bReverse.value_or(false);
	columns[colIndex].sortCaseSensitive = bCaseSensitive.value_or(false);
	sortColumns.clear();
	sortColumns.push_back(colIndex);
	for (int i = 0; i < (int)columns.size(); i++)
	{
		if (i != colIndex)
			sortColumns.push_back(i);
	}
	SortChanged();
}

void UListWindow::SortChanged()
{
	// Changing the keys with auto sort on sorts again.
	if (bAutoSort())
		Sort();
}

bool UListWindow::RowLess(const Item& a, const Item& b)
{
	// Each key in turn: a float or time column compares numbers, a string
	// column text with or without case, either reversed on request. Rows
	// equal in every key keep their order.
	for (int colIndex : sortColumns)
	{
		const Column& col = columns[colIndex];
		int cmp;
		if (col.type == ColTypeFloat || col.type == ColTypeTime)
		{
			float av = (size_t)colIndex < a.cells.size() ? a.cells[colIndex].value : 0.0f;
			float bv = (size_t)colIndex < b.cells.size() ? b.cells[colIndex].value : 0.0f;
			cmp = av < bv ? -1 : (av > bv ? 1 : 0);
		}
		else
		{
			const std::string& at = (size_t)colIndex < a.cells.size() ? a.cells[colIndex].text : std::string();
			const std::string& bt = (size_t)colIndex < b.cells.size() ? b.cells[colIndex].text : std::string();
			cmp = CompareText(at, bt, col.sortCaseSensitive);
		}
		if (col.sortReverse)
			cmp = -cmp;
		if (cmp != 0)
			return cmp < 0;
	}
	return false;
}

void UListWindow::Sort()
{
	// The focus and the anchor are rows, not places: they follow their rows.
	int focusId = (focusLine() >= 0 && (size_t)focusLine() < items.size()) ? items[focusLine()].id : 0;
	int anchorId = (anchorLine() >= 0 && (size_t)anchorLine() < items.size()) ? items[anchorLine()].id : 0;
	std::stable_sort(items.begin(), items.end(), [this](const Item& a, const Item& b) { return RowLess(a, b); });
	focusLine() = focusId ? RowIdToIndex(focusId) : -1;
	anchorLine() = anchorId ? RowIdToIndex(anchorId) : -1;
}

void UListWindow::ShowFocusRow()
{
	// Asks the parent to scroll the focus row into view.
	float lineHeight = GetLineHeight();
	if (focusLine() < 0 || lineHeight <= 0.0f)
		return;
	AskParentToShowArea(0.0f, focusLine() * lineHeight, Width(), lineHeight);
}

void UListWindow::ToggleRowSelection(int rowId)
{
	int rowIndex = RowIdToIndex(rowId);
	if (rowIndex == -1)
		return;
	items[rowIndex].selected = !items[rowIndex].selected;
}

void UListWindow::DrawWindow(UGC* gc)
{
	UFont* font = normalFont();
	if (!font)
		return;

	float w = Width();
	float lineHeight = GetLineHeight();
	lineSize() = lineHeight;

	float y = 0.0f;
	int lineIndex = 0;
	for (auto& item : items)
	{
		if (lineIndex == focusLine())
		{
			gc->SetTextColor(highlightTextColor);
			gc->SetTileColor(focusColor());
			if (focusTexture())
				gc->DrawTexture(0.0f, y, w, lineHeight, 0.0f, 0.0f, focusTexture());
		}

		if (item.selected)
		{
			float t = focusThickness();
			gc->SetTextColor(highlightTextColor);
			gc->SetTileColor(highlightColor());
			if (highlightTexture())
				gc->DrawTexture(t, y + t, std::max(w - 2.0f * t, 0.0f), std::max(lineHeight - t * 2.0f, 0.0f), 0.0f, 0.0f, highlightTexture());
		}
		else
		{
			gc->SetTextColor(TextColor());
			gc->SetTileColor(tileColor());
		}

		float x = 0.0f;
		for (int colIndex = 0; colIndex < (int)columns.size(); colIndex++)
		{
			auto& col = columns[colIndex];
			if (col.hidden)
				continue;
			if ((size_t)colIndex < item.cells.size())
			{
				UFont* colFont = col.font ? col.font : font;
				gc->SetFont(colFont);
				gc->SetAlignments((uint8_t)col.align, (uint8_t)EVAlign::Center);

				//if (!item.selected)
				//	gc->SetTextColor(col.color);

				gc->DrawText(x, y, col.width, lineHeight, FieldDisplayText(item, colIndex));
			}
			x += col.width;
		}
		y += lineHeight;
		lineIndex++;
	}
}

bool UListWindow::MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks)
{
	SetFocusWindow(this);

	if (UWindow::MouseButtonPressed(pointX, pointY, button, numClicks))
		return true;

	if (lineSize() <= 0.0f || items.empty())
		return true;

	// A click selects the row under the pointer, the last row when below
	// them all; a double click activates it.
	int index = (int)std::floor(pointY / lineSize());
	if (index >= (int)items.size())
		index = (int)items.size() - 1;
	int rowId = IndexToRowId(index);
	if (rowId > 0)
	{
		SetRow(rowId, true, true, false);
		SetFocusRow(rowId, false, false);
		anchorLine() = index;
		DispatchListSelectionChanged();
		if (numClicks == 2)
			ActivateRow();
	}

	return true;
}

bool UListWindow::MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks)
{
	return UWindow::MouseButtonReleased(pointX, pointY, button, numClicks);
}

bool UListWindow::VirtualKeyPressed(EInputKey key, bool bRepeat)
{
	if (UWindow::VirtualKeyPressed(key, bRepeat))
		return true;

	// Enter activates the focus row, as a double click does.
	if (key == IK_Enter)
	{
		ActivateRow();
		return true;
	}
	return false;
}

void UListWindow::ActivateRow()
{
	// ListRowActivated to the list's parents, with the activate sound.
	int rowId = GetFocusRow();
	if (rowId == 0)
		return;
	if (ActivateSound())
		PlaySound(ActivateSound(), {}, {}, {}, {});
	DispatchListRowActivated();
}

void UListWindow::DispatchListRowActivated()
{
	int rowId = GetFocusRow();
	for (UWindow* cur = this; cur; cur = cur->parentOwner())
	{
		if (cur->ListRowActivated(this, rowId))
			break;
	}
}

void UListWindow::DispatchListSelectionChanged()
{
	int numSelections = GetNumSelectedRows();
	int focusRowId = GetFocusRow();
	for (UWindow* cur = this; cur; cur = cur->parentOwner())
	{
		if (cur->ListSelectionChanged(this, numSelections, focusRowId))
			break;
	}
}
