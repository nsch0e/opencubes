#include <gtest/gtest.h>

#include "mapped_file.hpp"

TEST(Mapped, TestNonExistentFile) {
	auto file = std::make_shared<mapped::file>();
	EXPECT_LT(file->open("thisfileshouldnotexist"), 0);
	EXPECT_EQ(file->is_rw(), false);
	EXPECT_EQ(file->size(), 0);
}

TEST(Mapped, TestCreateFile) {
	using namespace mapped;
	auto file = std::make_shared<mapped::file>();
	EXPECT_GE(file->openrw("test_file.bin", 0, file::CREATE|file::RESIZE), 0);
	EXPECT_EQ(file->is_rw(), true);
	EXPECT_EQ(file->size(), 0);

	auto value = std::make_unique<struct_region<int>>(file, 0);

	// region constructor shall have side-effect of growing the file:
	EXPECT_EQ(file->size(), PAGE_SIZE);

	(**value) = 0;
	(**value)++;
	value->flush();
	auto eof = value->getEndSeek();
	value.reset();
	file->truncate(eof);
	EXPECT_EQ(file->size(), sizeof(int));
}

TEST(Mapped, TestReadData) {
	using namespace mapped;
	auto file = std::make_shared<mapped::file>();
	EXPECT_GE(file->open("test_file.bin"), 0);
	EXPECT_EQ(file->is_rw(), false);
	EXPECT_EQ(file->size(), sizeof(int));

	// read value form the previous test.
	auto value = std::make_unique<const struct_region<int>>(file, 0);

	// RO mapping shall not resize the file:
	EXPECT_EQ(file->size(), sizeof(int));
	EXPECT_EQ((**value), 1);
}
