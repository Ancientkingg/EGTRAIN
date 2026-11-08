#pragma once

#include "widgets/UserPrompts.h"

#include <QString>
#include <QStringList>

#include <optional>

/// Answers the prompts of an editor panel without a dialog and keeps what the panel asked.
class FakePrompts : public UserPrompts {
public:
	int errorCount = 0;
	QString errorTitle;
	QString errorMessage;
	bool errorWarning = false;

	int infoCount = 0;
	QString infoTitle;
	QString infoMessage;

	int confirmCount = 0;
	QString confirmTitle;
	QString confirmQuestion;
	bool confirmAnswer = false;

	int chooseCount = 0;
	QStringList offeredItems;
	QString chosenItem;

	int textCount = 0;
	QString textTitle;
	QString textLabel;
	std::optional<QString> answeredText;

	int buttonCount = 0;
	QString buttonTitle;
	QString buttonQuestion;
	QStringList offeredButtons;
	int defaultButton = -1;
	int chosenButton = -1;

	void showError(QWidget*, const QString& title, const QString& message, bool warningIcon = false) override {
		++errorCount;
		errorTitle = title;
		errorMessage = message;
		errorWarning = warningIcon;
	}
	void showInfo(QWidget*, const QString& title, const QString& message) override {
		++infoCount;
		infoTitle = title;
		infoMessage = message;
	}
	bool confirm(QWidget*, const QString& title, const QString& question) override {
		++confirmCount;
		confirmTitle = title;
		confirmQuestion = question;
		return confirmAnswer;
	}
	QString chooseItem(QWidget*, const QString&, const QString&, const QStringList& items) override {
		++chooseCount;
		offeredItems = items;
		return chosenItem;
	}
	std::optional<QString> askText(QWidget*, const QString& title, const QString& label, const QString&) override {
		++textCount;
		textTitle = title;
		textLabel = label;
		return answeredText;
	}
	int chooseButton(QWidget*, const QString& title, const QString& question, const QStringList& choices,
		int defaultChoice) override {
		++buttonCount;
		buttonTitle = title;
		buttonQuestion = question;
		offeredButtons = choices;
		defaultButton = defaultChoice;
		return chosenButton;
	}
};
