/**
* @file  cusdr_hpsdrTabWidget.h
* @brief Hardware settings widget header file for cuSDR
* @author Hermann von Hasseln, DL3HVH
* @version 0.1
* @date 2010-09-21
*/

/*
 *   
 *   Copyright 2010, 2011 Hermann von Hasseln, DL3HVH
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License version 2 as
 *   published by the Free Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
 
#ifndef _CUSDR_HPSDR_TABWIDGET_H
#define _CUSDR_HPSDR_TABWIDGET_H

//#include <QWidget>
//#include <QTabWidget>
//#include <QGroupBox>
//#include <QLineEdit>

#include "Util/cusdr_buttons.h"
#include "cusdr_settings.h"
#include "cusdr_hpsdrWidget.h"
#include "cusdr_networkWidget.h"
//#include "cusdr_transmitTabWidget.h"
#include "cusdr_alexTabWidget.h"
#include "cusdr_extCtrlWidget.h"


class HPSDRTabWidget : public QTabWidget {

	Q_OBJECT

public:
	HPSDRTabWidget(QWidget *parent = 0);
	~HPSDRTabWidget();


public slots:
	QSize	sizeHint() const;
	QSize	minimumSizeHint() const;
	void	addNICChangedConnection();
	
protected:
	void	closeEvent(QCloseEvent *event);
	void	showEvent(QShowEvent *event);
	void	enterEvent(QEvent *event);
	void	leaveEvent(QEvent *event);
	void	mouseMoveEvent(QMouseEvent *event);
	void	mousePressEvent(QMouseEvent *event);
	void	mouseReleaseEvent(QMouseEvent *event);

private:
	Settings			*set;

	QSDR::_Error				m_error;
	QSDR::_ServerMode			m_serverMode;
	QSDR::_HWInterfaceMode		m_hwInterface;
	QSDR::_DataEngineState		m_dataEngineState;

	HPSDRWidget			*m_hpsdrWidget;
	NetworkWidget		*m_networkWidget;
	AlexTabWidget		*m_alexTabWidget;
	ExtCtrlWidget		*m_extCtrlWidget;

	QString				m_message;

	int		m_minimumWidgetWidth;
	int		m_minimumGroupBoxWidth;

	void	setupConnections();

private slots:
	void systemStateChanged(
		QObject *sender,
		QSDR::_Error err,
		QSDR::_HWInterfaceMode hwmode,
		QSDR::_ServerMode mode,
		QSDR::_DataEngineState state);

	void setAlexPresence(bool value);
	void setPennyPresence(bool value);
	
signals:
	void	showEvent(QObject *sender);
	void	closeEvent(QObject *sender);
	void	messageEvent(QString message);
};

#endif // _CUSDR_HPSDR_TABWIDGET_H
