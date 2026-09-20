# Todolist

Open **Todolist** between **Settings** and **Pomodoro** on Home.

- **Add task** opens the device keyboard. Tap its OK key to save.
- Tap a checkbox to complete a task. It stays in the list with a checkmark
  and slightly gray, struck-through text, after all open tasks. Tap again to reopen it.
- Tap task text to open its details, edit it, mark it complete/open, or delete it.
- **Select** enters bulk selection. Choose tasks, then **Delete selected**.
  Selection is retained while paging. **Select all** selects the whole list.
- **Delete all** is available in Select mode. Deletion asks for confirmation;
  Cancel preserves the tasks and selection.
- Swipe up/down or use Previous/Next to page. Physical navigation buttons move
  focus between controls; Confirm activates the focused control. Back or the
  Home gesture exits the current detail/selection screen before leaving the list.

The list supports 64 tasks with up to 127 UTF-8 bytes per task. All tasks remain
available through pagination; completed tasks are never hidden or automatically
deleted. The interface and its keyboard use square corners.

## Persistence and resources

Tasks are stored under `/.crosspoint/todos.json` on the SD card. A mutation saves
the list; selection, scrolling, and rendering do not write to storage. Saving an
unchanged edit also avoids a write.

Writes go to `todos.tmp`, are read back and verified, then replace the current
file while preserving the previous valid file as `todos.bak`. Loading falls
back to a valid backup or temporary file if needed. Invalid or unreadable files
show an error instead of silently replacing the list with an empty one.

If a save fails, the current edits remain in RAM and the screen offers Retry.
Automatic sleep is held until those changes are saved or explicitly discarded.
Manual shutdown can still lose unsaved changes.

The model has fixed storage of about 8.3 KB, owned by the activity and released
when leaving it. Load/save temporarily allocate one checked model of the same
size to preserve the current list on parse failure and verify new writes.
The streaming reader/writer use 128-byte buffers instead of a JSON document or
whole-file string. The existing keyboard is allocated only during text entry.
There is no background task or per-frame task-list allocation.

## Verification

The host tests exercise the model and actual store implementation with a HAL
test double, including ordering, Unicode, bulk selection, limits, round trips,
recovery and injected I/O failures:

```sh
cmake -S test -B build/host-tests
cmake --build build/host-tests --target TodoListTest
ctest --test-dir build/host-tests -R '^(TodoListModel|TodoPersistence)\.' --output-on-failure
```

Build with `pio run -e simulator_x4_pro` for the desktop simulator and
`pio run -e x4pro` for X4 Pro firmware. On hardware, verify touch targets,
all four orientations, SD persistence across restart, and free heap while
opening and closing the keyboard. Simulator results do not measure physical
panel refresh behavior or SD-card power-loss guarantees.
