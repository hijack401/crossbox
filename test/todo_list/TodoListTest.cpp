#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

#include "TodoStore.h"

namespace {
constexpr char MAIN[] = "/.crosspoint/todos.json";
constexpr char TEMP[] = "/.crosspoint/todos.tmp";
constexpr char BACKUP[] = "/.crosspoint/todos.bak";
constexpr char VALID[] = R"({"version":1,"items":[{"text":"Original","completed":false}]})";

class TodoPersistence : public testing::Test {
 protected:
  void SetUp() override { todoFake::reset(); }
};
}  // namespace

TEST(TodoListModel, KeepsCompletedItemsAtBottomAndRestoresThemToOpenGroup) {
  TodoListModel model;
  ASSERT_TRUE(model.add("A"));
  ASSERT_TRUE(model.add("B"));
  ASSERT_TRUE(model.add("C"));
  ASSERT_TRUE(model.toggle(1));
  EXPECT_EQ(model.count(), 3u);
  EXPECT_EQ(model.openCount(), 2u);
  EXPECT_STREQ(model.item(0)->text, "A");
  EXPECT_STREQ(model.item(1)->text, "C");
  EXPECT_STREQ(model.item(2)->text, "B");
  EXPECT_TRUE(model.item(2)->completed);
  ASSERT_TRUE(model.toggle(0));
  EXPECT_STREQ(model.item(0)->text, "C");
  EXPECT_STREQ(model.item(1)->text, "B");
  EXPECT_STREQ(model.item(2)->text, "A");
  ASSERT_TRUE(model.add("D"));
  EXPECT_STREQ(model.item(1)->text, "D");
  ASSERT_TRUE(model.toggle(3));
  EXPECT_EQ(model.openCount(), 3u);
  EXPECT_STREQ(model.item(2)->text, "A");
  EXPECT_FALSE(model.item(2)->completed);
  EXPECT_STREQ(model.item(3)->text, "B");
}

TEST(TodoListModel, RejectsBlankControlInvalidUtf8AndOverlongTextWithoutChangingItems) {
  TodoListModel model;
  ASSERT_TRUE(model.add(" \t First task \r\n"));
  EXPECT_STREQ(model.item(0)->text, "First task");
  for (const char* invalid :
       {"", " \n\t ", "line\nbreak", "\x7f", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80"}) {
    EXPECT_FALSE(model.add(invalid));
    EXPECT_FALSE(model.update(0, invalid));
    EXPECT_STREQ(model.item(0)->text, "First task");
  }
  EXPECT_FALSE(model.add(nullptr));
  EXPECT_FALSE(model.add(std::string(128, 'A').c_str()));
  EXPECT_TRUE(model.add(std::string(127, 'A').c_str()));
  EXPECT_TRUE(model.add("Caf\xc3\xa9 \xf0\x9f\x93\x96"));
  EXPECT_EQ(model.count(), 3u);
}

TEST(TodoListModel, EditsPreserveCompletionAndInvalidIndicesAreHarmless) {
  TodoListModel model;
  ASSERT_TRUE(model.add("First"));
  ASSERT_TRUE(model.toggle(0));
  ASSERT_TRUE(model.update(0, " Revised "));
  EXPECT_STREQ(model.item(0)->text, "Revised");
  EXPECT_TRUE(model.item(0)->completed);
  EXPECT_FALSE(model.remove(1));
  EXPECT_FALSE(model.toggle(1));
  EXPECT_FALSE(model.update(1, "Invalid"));
  EXPECT_EQ(model.item(1), nullptr);
}

TEST(TodoListModel, BulkDeletionUsesOriginalIndicesIncludingBit63) {
  TodoListModel model;
  for (size_t i = 0; i < TodoListModel::MAX_ITEMS; ++i) ASSERT_TRUE(model.add(std::to_string(i).c_str()));
  EXPECT_FALSE(model.add("Overflow"));
  EXPECT_EQ(model.removeSelected((uint64_t{1} << 63) | (uint64_t{1} << 2) | 1), 3u);
  EXPECT_EQ(model.count(), 61u);
  EXPECT_STREQ(model.item(0)->text, "1");
  EXPECT_STREQ(model.item(1)->text, "3");
  EXPECT_STREQ(model.item(60)->text, "62");
  EXPECT_EQ(model.removeSelected(0), 0u);
  EXPECT_EQ(model.removeSelected(UINT64_MAX), 61u);
  EXPECT_EQ(model.openCount(), 0u);
  EXPECT_TRUE(model.add("Reusable"));
  model.clear();
  EXPECT_EQ(model.count(), 0u);
}

TEST_F(TodoPersistence, MissingFileStartsEmptyAndMutationsPersistOnlyWhenSaved) {
  TodoStore store;
  ASSERT_TRUE(store.load());
  EXPECT_EQ(store.loadStatus(), TodoStore::LoadStatus::Empty);
  EXPECT_FALSE(store.isReadOnly());
  ASSERT_TRUE(store.model().add("Task"));
  EXPECT_EQ(todoFake::writes, 0u);
  ASSERT_TRUE(store.save());
  TodoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_STREQ(reloaded.model().item(0)->text, "Task");
}

TEST_F(TodoPersistence, RoundTripsQuotesBackslashesUnicodeCompletedAndMaximumList) {
  TodoStore store;
  ASSERT_TRUE(store.load());
  ASSERT_TRUE(store.model().add("Read \"book\" in C:\\books: Caf\xc3\xa9 \xf0\x9f\x93\x96"));
  for (size_t i = 1; i < TodoListModel::MAX_ITEMS; ++i) ASSERT_TRUE(store.model().add(std::string(127, 'A').c_str()));
  ASSERT_TRUE(store.model().toggle(0));
  ASSERT_TRUE(store.save());
  TodoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.model().count(), 64u);
  EXPECT_EQ(reloaded.model().openCount(), 63u);
  EXPECT_TRUE(reloaded.model().item(63)->completed);
  EXPECT_STREQ(reloaded.model().item(63)->text, store.model().item(63)->text);
  reloaded.model().clear();
  ASSERT_TRUE(reloaded.save());
  TodoStore empty;
  ASSERT_TRUE(empty.load());
  EXPECT_EQ(empty.model().count(), 0u);
}

TEST_F(TodoPersistence, AcceptsReorderedFieldsAndUnicodeEscapes) {
  todoFake::files[MAIN] = R"( { "items": [ {"completed": true, "text": "Caf\u00e9 \ud83d\udcd6"},
    {"text":"Open", "completed":false}], "version":1 } )";
  TodoStore store;
  ASSERT_TRUE(store.load());
  EXPECT_STREQ(store.model().item(0)->text, "Open");
  EXPECT_STREQ(store.model().item(1)->text, "Caf\xc3\xa9 \xf0\x9f\x93\x96");
}

TEST_F(TodoPersistence, RejectsCorruptUnsupportedAndTruncatedFilesWithoutOverwriting) {
  for (const char* invalid : {"", "{}", R"({"version":2,"items":[]})", R"({"version":1,"items":null})",
                              R"({"version":1,"version":1,"items":[]})", R"({"version":1,"items":[],})",
                              R"({"version":1,"items":[{"text":"X","completed":0}]})",
                              R"({"version":1,"items":[{"text":"X","completed":true,"text":"Y"}]})",
                              R"({"version":1,"items":[{"text":"\u0000hidden","completed":false}]})",
                              R"({"version":1,"items":[{"text":"\ud800","completed":false}]})",
                              R"({"version":1,"items":[{"text":"","completed":false}]})",
                              R"({"version":1,"items":[]}garbage)", R"({"version":1,"items":[)"}) {
    todoFake::files[MAIN] = invalid;
    TodoStore store;
    EXPECT_FALSE(store.load()) << invalid;
    EXPECT_TRUE(store.isReadOnly());
    EXPECT_FALSE(store.save());
    EXPECT_EQ(todoFake::files[MAIN], invalid);
  }
}

TEST_F(TodoPersistence, RejectsOversizeFilesAndTooManyItems) {
  todoFake::files[MAIN] = std::string(65537, ' ');
  TodoStore store;
  EXPECT_FALSE(store.load());
  std::string json = R"({"version":1,"items":[)";
  for (size_t i = 0; i < 65; ++i) {
    if (i) json += ',';
    json += R"({"text":"Task","completed":false})";
  }
  todoFake::files[MAIN] = json + "]}";
  EXPECT_FALSE(store.load());
}

TEST_F(TodoPersistence, UsesBackupAfterInterruptedRenameOrCorruptPrimary) {
  todoFake::files[MAIN] = "corrupt";
  todoFake::files[BACKUP] = VALID;
  TodoStore store;
  ASSERT_TRUE(store.load());
  EXPECT_EQ(store.loadStatus(), TodoStore::LoadStatus::Recovered);
  EXPECT_STREQ(store.model().item(0)->text, "Original");
  ASSERT_TRUE(store.model().add("New"));
  ASSERT_TRUE(store.save());
  EXPECT_EQ(todoFake::files[BACKUP], VALID);
  TodoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.model().count(), 2u);
}

TEST_F(TodoPersistence, PreservesRecoveredTemporaryBeforeAnotherWrite) {
  todoFake::files[TEMP] = VALID;
  TodoStore store;
  ASSERT_TRUE(store.load());
  ASSERT_TRUE(store.model().add("New"));
  todoFake::writeRemaining = 8;
  EXPECT_FALSE(store.save());
  EXPECT_EQ(todoFake::files[BACKUP], VALID);
  TodoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.model().count(), 1u);
  EXPECT_STREQ(reloaded.model().item(0)->text, "Original");
}

TEST_F(TodoPersistence, FailedWriteVerificationCloseOrRenamePreservesPreviousDiskList) {
  for (unsigned failure = 0; failure < 6; ++failure) {
    todoFake::reset();
    todoFake::files[MAIN] = VALID;
    TodoStore store;
    ASSERT_TRUE(store.load());
    ASSERT_TRUE(store.model().add("New"));
    if (failure == 0) todoFake::writeRemaining = 10;
    if (failure == 1) todoFake::failRead = true;
    if (failure == 2) todoFake::failClose = true;
    if (failure == 3) todoFake::failRenameAt = 0;
    if (failure == 4) todoFake::failRenameAt = 1;
    if (failure == 5) todoFake::failDirectory = true;
    EXPECT_FALSE(store.save()) << failure;
    EXPECT_EQ(todoFake::files[MAIN], VALID) << failure;
    EXPECT_EQ(store.model().count(), 2u);
    todoFake::writeRemaining = -1;
    todoFake::failRead = false;
    todoFake::failClose = false;
    todoFake::failRenameAt = -1;
    todoFake::failDirectory = false;
    ASSERT_TRUE(store.save()) << failure;
    TodoStore reloaded;
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.model().count(), 2u);
  }
}

TEST_F(TodoPersistence, UnavailableStorageIsReadOnlyAndFailedReloadPreservesMemory) {
  todoFake::ready = false;
  TodoStore store;
  EXPECT_FALSE(store.load());
  EXPECT_TRUE(store.isReadOnly());
  todoFake::ready = true;
  todoFake::files[MAIN] = VALID;
  ASSERT_TRUE(store.load());
  todoFake::files[MAIN] = "corrupt";
  EXPECT_FALSE(store.load());
  EXPECT_STREQ(store.model().item(0)->text, "Original");
}
