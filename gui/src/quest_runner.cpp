/*
 * Copyright (C) 2006-2019 Christopho, Solarus - http://www.solarus-games.org
 *
 * Solarus Quest Editor is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Solarus Quest Editor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "solarus/gui/quest_runner.h"
#include "solarus/gui/settings.h"
#include <QApplication>
#include <QMessageBox>
#include <QSize>
#include <QTimer>

namespace SolarusGui {

/**
 * @brief Creates a quest runner.
 * @param parent The parent object of the thread.
 */
QuestRunner::QuestRunner(QObject* parent) :
  QObject(parent),
  process(this),
  last_command_id(-1) {

  // Set the process channel mode to merged (stdout + stderr)
  process.setProcessChannelMode(QProcess::MergedChannels);

  // Connect to QProcess signals to know when the quest is running and finished.
  connect(&process, SIGNAL(started()),
          this, SIGNAL(running()));
  connect(&process, SIGNAL(finished(int)),
          this, SLOT(on_finished()));
  connect(&process, SIGNAL(error(QProcess::ProcessError)),
          this, SLOT(on_error(QProcess::ProcessError)));
  connect(&process, SIGNAL(readyReadStandardOutput()),
          this, SLOT(standard_output_data_available()));

  // Workaround to make the quest process close properly instead of hanging
  // while reading on its stdin on windows.
  QTimer* timer = new QTimer(this);
  connect(timer, &QTimer::timeout, [this] () {
    if (is_started()) {
      process.write("\n");
    }
  });
  timer->start(100);
}

/**
 * @brief Destroys the quest runner.
 *
 * If a quest is running, terminates it.
 */
QuestRunner::~QuestRunner() {

  if (process.state() == QProcess::Running) {
    // Give a chance to the quest process to finish properly.
    process.terminate();
    if (!process.waitForFinished(1000)) {
      // Kill it after a delay.
      process.kill();
    }
  }
}

/**
 * @brief Creates and returns the list of arguments to pass to the process.
 * @param quest_path The path of the quest to run.
 * @param map_id A map to run, or an empty string to run the quest normally.
 */
QStringList QuestRunner::create_arguments(
    const QString& quest_path, const QString& map_id) const {

  QStringList arguments;

  Settings settings;

  // -run quest_path
  arguments << "-run";

  if (!map_id.isEmpty()) {
    arguments << QString("-map=%1").arg(map_id);
  }

  // no-audio
  if (settings.value("no_audio", false).toBool()) {
    arguments << "-no-audio";
  }

  // force-software-rendering
  if (settings.value("force_software_rendering", false).toBool()) {
    arguments << "-force-software-rendering";
  }

  // suspend-unfocused
  if (!settings.value("suspend_unfocused", true).toBool()) {
    arguments << "-suspend-unfocused=no";
  }

  // quest-size
  const QSize size = settings.value("quest_size").toSize();
  if (size.isValid()) {
    QString size_str = QString::number(size.width()) + "x" +
        QString::number(size.height());
    arguments << "-quest-size=" + size_str;
  }

  // Path of the quest.
  arguments << quest_path;

  return arguments;
}

/**
 * @brief Returns whether the quest is started.
 *
 * The quest is started as soon as you call start(),
 * but it then takes a slight delay for the process to actually load and run.
 * The signal running() is emitted when the process is running.
 *
 * @return @c true if the quest is started.
 */
bool QuestRunner::is_started() const {

  return process.state() != QProcess::NotRunning;
}

/**
 * @brief Returns whether the quest is running.
 * @return @c true if the quest is running.
 */
bool QuestRunner::is_running() const {

  return process.state() == QProcess::Running;
}

/**
 * @brief Runs a specific quest.
 * @param quest_path The path of the quest to run.
 * @param map_id A map to run, or an empty string to run the quest normally.
 *
 * Does nothing if the path is empty or if a quest is already running.
 *
 * This function returns immediately.
 * The signal running() is emitted when the process actually runs.
 */
void QuestRunner::start(const QString& quest_path, const QString& map_id) {

  if (quest_path.isEmpty()) {
    return;
  }

  if (is_started()) {
    return;
  }

  // Run the current executable itself with the special option "-run quest_path".
  QString program_name = QApplication::applicationFilePath();
  QStringList arguments = create_arguments(quest_path, map_id);

  process.start(program_name, arguments);
}

/**
 * @brief Stops the current quest.
 *
 * Returns immediately.
 * The signal finished() is emitted when the process is finished.
 */
void QuestRunner::stop() {

  if (is_started()) {
    process.terminate();
  }
}

/**
 * @brief Slot called when lines are written on the standard output of the
 * quest process.
 */
void QuestRunner::standard_output_data_available() {

  // Read the UTF-8 data available.
  QStringList lines;
  while (process.canReadLine()) {
    QString line(process.readLine());
    line = line.trimmed();  // Remove the final '\n'.
    if (!line.isEmpty()) {
      lines << line;
    }
  }

  if (!lines.isEmpty()) {
    emit output_produced(lines);
  }
}

/**
 * @brief Executes some Lua code in the quest process.
 * @param command The Lua code.
 * @return The id of the command executed, or -1 if it could not be sent
 * to the process.
 */
int QuestRunner::execute_command(const QString& command) {

  if (!is_running()) {
    return -1;
  }

  if (command.isEmpty()) {
    return -1;
  }

  QByteArray command_utf8 = command.toUtf8();
  command_utf8.append("\n");
  qint64 bytes_written = process.write(command_utf8);
  if (bytes_written != command_utf8.size()) {
    return -1;
  }

  ++last_command_id;
  return last_command_id;
}

/**
 * @brief Slot called when the execution finishes.
 */
void QuestRunner::on_finished() {

  last_command_id = -1;
  emit finished();
}

/**
 * @brief Slot called when there is a process execution error.
 * @param perr the process error to report to the user.
 *
 * This function simply notifies the user of an error and returns immediately.
 */
void QuestRunner::on_error(QProcess::ProcessError perr) {

  switch (perr) {
    case QProcess::FailedToStart:
      QMessageBox::critical(nullptr, tr("Quest Runner"),
          tr("The process failed to start."));
      break;
    case QProcess::Crashed:
      QMessageBox::critical(nullptr, tr("Quest Runner"),
          tr("The process crashed some time after starting successfully."));
      break;
    case QProcess::Timedout:
      QMessageBox::critical(nullptr, tr("Quest Runner"),
          tr("The last wait-for function on the process timed out."));
      break;
    case QProcess::WriteError:
      QMessageBox::critical(nullptr, tr("Quest Runner"),
          tr("An error occurred when attempting to write to the process."));
      break;
    case QProcess::ReadError:
      QMessageBox::critical(nullptr, tr("Quest Runner"),
          tr("An error occurred when attempting to read from the process."));
      break;
    case QProcess::UnknownError:
      QMessageBox::critical(nullptr, tr("Quest Runner"),
          tr("An unknown error occurred."));
      break;
  }
}

}
