#pragma once
// Self-contained X3 bank resources. The three source-verified filter tables
// are embedded in the Windows VST3 PE as RCDATA resource IDs 2001..2003.
// The research parser currently exposes file-only constructors. Temporarily
// materialize resources on the *control thread* and remove them immediately
// after the parser has loaded its own in-memory coefficient tables.
// No bank installation, user profile folder or environment variable is needed.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <filesystem>
#include <system_error>

namespace zplane {
class EmbeddedX3Resources final {
 std::filesystem::path temporary_{};
 static bool copyResource(HMODULE dll,unsigned identifier,const wchar_t* filename,
                          DWORD expectedSize,const std::filesystem::path& dir) {
  const auto found=FindResourceW(dll,MAKEINTRESOURCEW(identifier),RT_RCDATA);
  if(!found||SizeofResource(dll,found)!=expectedSize)return false;
  const auto resource=LoadResource(dll,found);
  if(!resource)return false;
  const void* bytes=LockResource(resource);
  if(!bytes)return false;
  const auto dest=dir/filename;
  const HANDLE file=CreateFileW(dest.c_str(),GENERIC_WRITE,0,nullptr,
                                CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false;
  DWORD written=0;
  const BOOL success=WriteFile(file,bytes,expectedSize,&written,nullptr);
  const BOOL closed=CloseHandle(file);
  return success&&closed&&written==expectedSize;
 }
public:
 EmbeddedX3Resources()=default;
 EmbeddedX3Resources(const EmbeddedX3Resources&)=delete;
 EmbeddedX3Resources& operator=(const EmbeddedX3Resources&)=delete;
 ~EmbeddedX3Resources() {
  if(!temporary_.empty()){
   std::error_code ignored;
   std::filesystem::remove_all(temporary_,ignored);
  }
 }
 bool extract(HMODULE dll) {
  if(!dll)return false;
  wchar_t base[MAX_PATH]{},unique[MAX_PATH]{};
  const DWORD chars=GetTempPathW(MAX_PATH,base);
  if(!chars||chars>=MAX_PATH||!GetTempFileNameW(base,L"ZPF",0,unique))return false;
  if(!DeleteFileW(unique)||!CreateDirectoryW(unique,nullptr))return false;
  temporary_=unique;
  return copyResource(dll,2001,L"factory-bank.bin",31680,temporary_)&&
         copyResource(dll,2002,L"standard-bank.bin",4960,temporary_)&&
         copyResource(dll,2003,L"morph-support.bin",402,temporary_);
 }
 const std::filesystem::path& directory() const noexcept {return temporary_;}
};
} // namespace zplane
