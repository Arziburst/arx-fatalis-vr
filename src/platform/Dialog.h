/*
 * Copyright 2011-2021 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARX_PLATFORM_DIALOG_H
#define ARX_PLATFORM_DIALOG_H

#include <string>
#include <string_view>

namespace platform {

enum DialogType {
	DialogInfo,
	DialogWarning,
	DialogError,
	DialogYesNo,
	DialogWarnYesNo,
	DialogOkCancel
};

/*!
 * \brief Show an informative message box
 *
 * \param message text of the message to display.
 * \param title   title for the dialog window.
 *
 * A simple markup syntax is supported on some systems. On other systems, the message is
 * displayed as-is:
 *
 *  * Lines staring with " * " are turned into bulleted lists.
 *
 *  * URLs starting with "http://" or "https://" are turned into links.
 *
 *  * Text surrounded by quotes is made bold.
 *
 *  * Lines starting with "-> " are made italic.
 *
 * This function does not return until the user closes the dialog.
 *
 * \note This may not be supported on all platforms. If dialogs are not supported, the
 *       message is printed to stdout and the function returns immediately.
 */
void showInfoDialog(const std::string & message, const std::string & title = "Information");

/*!
 * \brief Show a warning message box
 *
 * \param message text of the message to display.
 * \param title   title for the dialog window.
 *
 * A simple markup syntax is supported on some systems. On other systems, the message is
 * displayed as-is. See \ref showInfoDialog for the markup syntax.
 *
 * This function does not return until the user closes the dialog.
 *
 * \note This may not be supported on all platforms. If dialogs are not supported, the
 *       message is printed to stdout and the function returns immediately.
 */
void showWarningDialog(const std::string & message, const std::string & title = "Warning");


/*!
 * \brief Show an error message box
 *
 * \param message text of the message to display.
 * \param title   title for the dialog window.
 *
 * A simple markup syntax is supported on some systems. On other systems, the message is
 * displayed as-is. See \ref showInfoDialog for the markup syntax.
 *
 * This function does not return until the user closes the dialog.
 *
 * \note This may not be supported on all platforms. If dialogs are not supported, the
 *       message is printed to stdout and the function returns immediately.
 */
void showErrorDialog(const std::string & message, const std::string & title = "Error");

/*!
 * \brief Show a dialog asking a Yes/No question
 *
 * \param question text of the question to display.
 * \param title    title for the dialog window.
 *
 * A simple markup syntax is supported on some systems. On other systems, the message is
 * displayed as-is. See \ref showInfoDialog for the markup syntax.
 *
 * \note this may not be supported on all platforms. If dialogs are not supported, the
 *       message is printed to stdout and the function immediately returns \c true.
 *
 * \return \c true if the user clicked "Yes", or \c false if the user clicked "No" or
 *         closed the dialog.
 */
bool askYesNo(const std::string & question, const std::string & title = "Yes/No?");

/*!
 * \brief Show an error dialog asking a Yes/No question
 *
 * \param question text of the question to display.
 * \param title    title for the dialog window.
 *
 * A simple markup syntax is supported on some systems. On other systems, the message is
 * displayed as-is. See \ref showInfoDialog for the markup syntax.
 *
 * \note this may not be supported on all platforms. If dialogs are not supported, the
 *       message is printed to stdout and the function immediately returns \c true.
 *
 * \return \c true if the user clicked "Yes", or \c false if the user clicked "No" or
 *         closed the dialog.
 */
bool askYesNoWarning(const std::string & question, const std::string & title = "Warning");

/*!
 * \brief Show a dialog asking a Ok/Cancel question
 *
 * \param question text of the question to display.
 * \param title    title for the dialog window.
 *
 * A simple markup syntax is supported on some systems. On other systems, the message is
 * displayed as-is. See \ref showInfoDialog for the markup syntax.
 *
 * \note this may not be supported on all platforms. If dialogs are not supported, the
 *       message is printed to stdout and the function immediately returns \c true.
 *
 * \return \c true if the user clicked "Ok", or \c false if the user clicked "Cancel" or
 *         closed the dialog.
 */
bool askOkCancel(const std::string & question, const std::string & title = "Ok/Cancel?");

/*!
 * \brief Whether this run shows no dialogs (#501)
 *
 * Scripted and test launches set \c ARXVR_NO_DIALOGS=1 (the test channel sets \c ARX_UNATTENDED=1,
 * which counts too): nobody is there to click a message box. Every dialog is then written to the log -
 * and through it to stderr - as one line (\ref formatDialogLine) and answered with its default
 * (\ref dialogDefaultAnswer) instead of being shown.
 */
bool dialogsDisabled();

/*!
 * \brief Whether an error dialog was answered without being shown
 *
 * The game then exits with a failure code, so the script that started it sees the error.
 */
bool errorDialogSuppressed();

//! The decision of \ref dialogsDisabled from the two environment values (\c nullptr when not set)
inline bool dialogsDisabledBy(const char * noDialogs, const char * unattended) {
	bool noDialogsSet = noDialogs && *noDialogs && std::string_view(noDialogs) != "0";
	return noDialogsSet || (unattended && *unattended);
}

//! The name of a dialog type in the line that replaces the dialog
inline std::string_view dialogTypeName(DialogType type) {
	switch(type) {
		case DialogInfo:      return "info";
		case DialogWarning:   return "warning";
		case DialogError:     return "error";
		case DialogYesNo:     return "yes-no";
		case DialogWarnYesNo: return "warn-yes-no";
		case DialogOkCancel:  return "ok-cancel";
	}
	return "unknown";
}

/*!
 * \brief The answer to a dialog that is not shown
 *
 * A message is acknowledged (\c true, "OK"); a question gets the answer of closing the dialog - "No" or
 * "Cancel" (\c false), so nothing it offers to do (run the data installer, ...) is done unasked.
 */
inline bool dialogDefaultAnswer(DialogType type) {
	return type == DialogInfo || type == DialogWarning || type == DialogError;
}

/*!
 * \brief The single line written instead of a dialog
 *
 * "[dialog] <type>: <title>: <message>", the message's line breaks (and blank lines) joined into one
 * " | ", and for a question the answer given: " [answered: no]" or " [answered: cancel]".
 */
inline std::string formatDialogLine(DialogType type, std::string_view title, std::string_view message) {

	std::string line = "[dialog] ";
	line += dialogTypeName(type);
	line += ": ";
	line += title;
	line += ": ";

	bool text = false;
	bool lineBreak = false;
	for(char c : message) {
		if(c == '\n' || c == '\r') {
			lineBreak = text;
			continue;
		}
		if(lineBreak) {
			line += " | ";
			lineBreak = false;
		}
		line += c;
		text = true;
	}

	if(!dialogDefaultAnswer(type)) {
		line += (type == DialogOkCancel) ? " [answered: cancel]" : " [answered: no]";
	}

	return line;
}

} // namespace platform

#endif // ARX_PLATFORM_DIALOG_H
