#include <tess/storage/chunk_page.h>

#include <string>

struct NameTag {};

using InvalidField = tess::Field<NameTag, std::string>;

int main() { static_cast<void>(sizeof(InvalidField)); }
