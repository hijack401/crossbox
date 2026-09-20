#pragma once

#include "util/TodoListModel.h"

class TodoStore {
 public:
  enum class LoadStatus { NotLoaded, Empty, Loaded, Recovered, Error };

  TodoListModel& model() { return items; }
  const TodoListModel& model() const { return items; }
  bool load();
  bool save();
  bool isReadOnly() const { return status == LoadStatus::NotLoaded || status == LoadStatus::Error; }
  LoadStatus loadStatus() const { return status; }

 private:
  TodoListModel items;
  LoadStatus status = LoadStatus::NotLoaded;
  bool primaryValid = false;
  bool recoveredFromTemporary = false;
};
