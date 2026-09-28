#ifndef __UITILELAYOUT_H__
#define __UITILELAYOUT_H__

#pragma once

namespace DuiLib
{
	class DUILIB_API CTileLayoutUI : public CContainerUI
	{
	public:
		CTileLayoutUI();

		LPCTSTR GetClass() const;
		LPVOID GetInterface(LPCTSTR pstrName);

		void SetPos(RECT rc, bool bNeedInvalidate = true);

		int GetFixedColumns() const;
		void SetFixedColumns(int iColums);
		int GetChildVPadding() const;
		void SetChildVPadding(int iPadding);

		SIZE GetItemSize() const;
		void SetItemSize(SIZE szSize);
		int GetColumns() const;
		int GetRows() const;
		// FastFile: vertical flow ("columnfirst") fills a column top->bottom before
		// wrapping to the next one; rows come from the available height (list view).
		bool IsColumnFirst() const;
		void SetColumnFirst(bool bColumnFirst);
		void SetAttribute(LPCTSTR pstrName, LPCTSTR pstrValue);

	protected:
		SIZE m_szItem;
		int m_nColumns;
		int m_nRows;

		int m_nColumnsFixed;
		int m_iChildVPadding;
		bool m_bIgnoreItemPadding;
		bool m_bColumnFirst;
	};
}
#endif // __UITILELAYOUT_H__
