#include "rar.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/cpuset.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#define UNRAR_DATA_DIR "/data/unrar"
#define UNRAR_CONFIG UNRAR_DATA_DIR "/config.ini"
#define UNRAR_STAGE_DIR UNRAR_DATA_DIR "/staging"
#define DEFAULT_EXTRACT_LOCATION "/data/homebrew"

typedef struct notify_request
{
  char unused[45];
  char message[3075];
} notify_request_t;

extern "C" int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

struct PayloadConfig
{
  std::string filename;
  std::string rar_location;
  std::string rar_password;
  bool delete_after;
  std::string extract_location;
  uint threads;
  int nice;
  uint64 cpu_mask;
};

struct LockGuard
{
  int fd;
  std::string path;

  LockGuard():fd(-1),path(UNRAR_DATA_DIR "/unrar.lock") {}
  ~LockGuard()
  {
    if (fd>=0)
      close(fd);
  }

  bool Acquire()
  {
    fd=open(path.c_str(),O_WRONLY|O_CREAT,0666);
    if (fd<0)
      return false;

    struct flock Lock;
    memset(&Lock,0,sizeof(Lock));
    Lock.l_type=F_WRLCK;
    Lock.l_whence=SEEK_SET;
    if (fcntl(fd,F_SETLK,&Lock)!=0)
    {
      close(fd);
      fd=-1;
      return false;
    }

    ftruncate(fd,0);
    char Pid[32];
    int Len=snprintf(Pid,sizeof(Pid),"%d\n",(int)getpid());
    if (Len>0)
      write(fd,Pid,(size_t)Len);
    return true;
  }
};

static int LastNotifiedProgress=-10;

static bool EndsWithNoCase(const std::string &Value,const std::string &Suffix)
{
  if (Value.size()<Suffix.size())
    return false;
  size_t Offset=Value.size()-Suffix.size();
  for (size_t I=0;I<Suffix.size();I++)
    if (std::tolower((unsigned char)Value[Offset+I])!=std::tolower((unsigned char)Suffix[I]))
      return false;
  return true;
}

static std::string Trim(const std::string &Value)
{
  size_t Start=0;
  while (Start<Value.size() && std::isspace((unsigned char)Value[Start]))
    Start++;

  size_t End=Value.size();
  while (End>Start && std::isspace((unsigned char)Value[End-1]))
    End--;

  return Value.substr(Start,End-Start);
}

static std::string ToLowerAscii(const std::string &Value)
{
  std::string Lower=Value;
  for (size_t I=0;I<Lower.size();I++)
    Lower[I]=(char)std::tolower((unsigned char)Lower[I]);
  return Lower;
}

static std::string JoinPath(const std::string &Left,const std::string &Right)
{
  if (Left.empty() || Right.empty() || Right[0]=='/')
    return Left.empty() ? Right:Left;
  if (Left[Left.size()-1]=='/')
    return Left+Right;
  return Left+"/"+Right;
}

static std::string BaseName(const std::string &Path)
{
  size_t Pos=Path.find_last_of('/');
  return Pos==std::string::npos ? Path:Path.substr(Pos+1);
}

static bool LooksLikeTitleIdAt(const std::string &Text,size_t Pos)
{
  if (Pos+9>Text.size())
    return false;
  for (size_t I=0;I<4;I++)
    if (!std::isupper((unsigned char)Text[Pos+I]))
      return false;
  for (size_t I=4;I<9;I++)
    if (!std::isdigit((unsigned char)Text[Pos+I]))
      return false;
  return true;
}

static bool FindTitleIdInText(const std::string &Text,std::string &TitleId)
{
  for (size_t I=0;I<Text.size();I++)
  {
    if (LooksLikeTitleIdAt(Text,I))
    {
      TitleId=Text.substr(I,9);
      return true;
    }
  }
  return false;
}

static std::string DirName(const std::string &Path)
{
  size_t Pos=Path.find_last_of('/');
  if (Pos==std::string::npos)
    return ".";
  if (Pos==0)
    return "/";
  return Path.substr(0,Pos);
}

static bool PathExists(const std::string &Path);
static bool RemoveTree(const std::string &Path,std::string &Error);
static void LogLine(const char *Fmt,...);

static bool RemoveExistingInstallBeforeExtract(const PayloadConfig &Cfg,
                                               const std::string &ArchivePath,
                                               std::string &Error)
{
  std::string TitleId;
  if (!FindTitleIdInText(BaseName(ArchivePath),TitleId) &&
      !FindTitleIdInText(Cfg.filename,TitleId))
    return true;

  std::string FinalPath=JoinPath(Cfg.extract_location,TitleId+"-app");
  if (!PathExists(FinalPath))
    return true;

  LogLine("pre_remove final_path=%s",FinalPath.c_str());
  return RemoveTree(FinalPath,Error);
}

static bool PathExists(const std::string &Path)
{
  struct stat St;
  return stat(Path.c_str(),&St)==0;
}

static bool IsDirPath(const std::string &Path)
{
  struct stat St;
  return stat(Path.c_str(),&St)==0 && S_ISDIR(St.st_mode);
}

static bool MkdirAll(const std::string &Path)
{
  if (Path.empty() || Path=="/")
    return true;

  std::string Cur;
  size_t Pos=Path[0]=='/' ? 1:0;
  if (Path[0]=='/')
    Cur="/";

  while (Pos<=Path.size())
  {
    size_t Next=Path.find('/',Pos);
    std::string Part=Path.substr(Pos,Next==std::string::npos ? std::string::npos:Next-Pos);
    if (!Part.empty())
    {
      if (!Cur.empty() && Cur[Cur.size()-1]!='/')
        Cur+="/";
      Cur+=Part;
      if (mkdir(Cur.c_str(),0777)!=0 && errno!=EEXIST)
        return false;
    }
    if (Next==std::string::npos)
      break;
    Pos=Next+1;
  }
  return true;
}

static bool ReadSmallFile(const std::string &Path,std::string &Data,size_t MaxSize=1024*1024)
{
  int Fd=open(Path.c_str(),O_RDONLY);
  if (Fd<0)
    return false;

  Data.clear();
  char Buf[4096];
  while (Data.size()<MaxSize)
  {
    ssize_t ReadSize=read(Fd,Buf,sizeof(Buf));
    if (ReadSize<0)
    {
      close(Fd);
      return false;
    }
    if (ReadSize==0)
      break;
    Data.append(Buf,(size_t)ReadSize);
  }
  close(Fd);
  return true;
}

static bool WriteTextFile(const std::string &Path,const char *Data)
{
  int Fd=open(Path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0666);
  if (Fd<0)
    return false;

  size_t Len=strlen(Data);
  const char *Ptr=Data;
  while (Len>0)
  {
    ssize_t Written=write(Fd,Ptr,Len);
    if (Written<=0)
    {
      close(Fd);
      return false;
    }
    Ptr+=Written;
    Len-=(size_t)Written;
  }
  close(Fd);
  return true;
}

static void Notify(const char *Fmt,...)
{
  notify_request_t Req;
  memset(&Req,0,sizeof(Req));

  va_list Args;
  va_start(Args,Fmt);
  vsnprintf(Req.message,sizeof(Req.message),Fmt,Args);
  va_end(Args);

  sceKernelSendNotificationRequest(0,&Req,sizeof(Req),0);
}

static void LogLine(const char *Fmt,...)
{
  if (!MkdirAll(UNRAR_DATA_DIR))
    return;

  int Fd=open(UNRAR_DATA_DIR "/unrar.log",O_WRONLY|O_CREAT|O_APPEND,0666);
  if (Fd<0)
    return;

  char Msg[2048];
  va_list Args;
  va_start(Args,Fmt);
  int Len=vsnprintf(Msg,sizeof(Msg),Fmt,Args);
  va_end(Args);

  if (Len>0)
  {
    size_t Size=(size_t)std::min(Len,(int)sizeof(Msg)-2);
    Msg[Size++]='\n';
    write(Fd,Msg,Size);
  }
  close(Fd);
}

static uint64 NowMs()
{
  struct timespec Ts;
  if (clock_gettime(CLOCK_MONOTONIC,&Ts)!=0)
    return 0;
  return (uint64)Ts.tv_sec*1000+(uint64)Ts.tv_nsec/1000000;
}

extern "C" void Ps5UnrarProgress(int Percent)
{
  if (Percent<0)
    return;
  if (Percent>100)
    Percent=100;

  int Bucket=(Percent/10)*10;
  if (Bucket>=LastNotifiedProgress+10)
  {
    LastNotifiedProgress=Bucket;
    Notify("UnRAR: extraction %d%%",Bucket);
  }
}

static bool EnsureDefaultConfig(std::string &Error)
{
  if (!MkdirAll(UNRAR_DATA_DIR))
  {
    Error="failed to create " UNRAR_DATA_DIR;
    return false;
  }

  if (PathExists(UNRAR_CONFIG))
    return true;

  static const char DefaultConfig[]=
    "filename=\n"
    "rar_location=/data/unrar\n"
    "rar_password=\n"
    "delete_after=0\n"
    "extract_location=/data/homebrew\n"
    "threads=0\n"
    "nice=-20\n"
    "cpu_mask=0\n";

  if (!WriteTextFile(UNRAR_CONFIG,DefaultConfig))
  {
    Error="failed to create " UNRAR_CONFIG;
    return false;
  }
  return true;
}

static bool LoadConfig(PayloadConfig &Cfg,std::string &Error)
{
  Cfg.filename.clear();
  Cfg.rar_location=UNRAR_DATA_DIR;
  Cfg.rar_password.clear();
  Cfg.delete_after=false;
  Cfg.extract_location=DEFAULT_EXTRACT_LOCATION;
  Cfg.threads=0;
  Cfg.nice=-20;
  Cfg.cpu_mask=0;

  if (!EnsureDefaultConfig(Error))
    return false;

  std::string Data;
  if (!ReadSmallFile(UNRAR_CONFIG,Data))
  {
    Error="failed to read " UNRAR_CONFIG;
    return false;
  }

  size_t Pos=0;
  while (Pos<Data.size())
  {
    size_t End=Data.find('\n',Pos);
    std::string Line=Data.substr(Pos,End==std::string::npos ? std::string::npos:End-Pos);
    Pos=End==std::string::npos ? Data.size():End+1;

    Line=Trim(Line);
    if (Line.empty() || Line[0]=='#' || Line[0]==';')
      continue;

    size_t Eq=Line.find('=');
    if (Eq==std::string::npos)
      continue;

    std::string Key=Trim(Line.substr(0,Eq));
    std::string Value=Trim(Line.substr(Eq+1));
    if (Key=="filename")
      Cfg.filename=Value;
    else if (Key=="rar_location" && !Value.empty())
      Cfg.rar_location=Value;
    else if (Key=="rar_password")
      Cfg.rar_password=Value;
    else if (Key=="delete_after")
      Cfg.delete_after=Value=="1" || Value=="true" || Value=="yes" || Value=="on";
    else if (Key=="extract_location" && !Value.empty())
      Cfg.extract_location=Value;
    else if (Key=="threads")
      Cfg.threads=(uint)strtoul(Value.c_str(),NULL,10);
    else if (Key=="nice")
      Cfg.nice=atoi(Value.c_str());
    else if (Key=="cpu_mask")
      Cfg.cpu_mask=strtoull(Value.c_str(),NULL,0);
  }

  return true;
}

static void ApplySchedulingConfig(const PayloadConfig &Cfg)
{
  if (Cfg.nice>=-20 && Cfg.nice<=20)
  {
    if (setpriority(PRIO_PROCESS,0,Cfg.nice)==0)
      LogLine("priority nice=%d result=ok",Cfg.nice);
    else
      LogLine("priority nice=%d result=fail errno=%d",Cfg.nice,errno);
  }

  if (Cfg.cpu_mask!=0)
  {
    cpuset_t Mask;
    CPU_ZERO(&Mask);
    for (uint Cpu=0;Cpu<64;Cpu++)
      if ((Cfg.cpu_mask & ((uint64)1<<Cpu))!=0)
        CPU_SET(Cpu,&Mask);

    if (cpuset_setaffinity(CPU_LEVEL_WHICH,CPU_WHICH_PID,getpid(),sizeof(Mask),&Mask)==0)
      LogLine("cpu_mask=0x%llx result=ok",(unsigned long long)Cfg.cpu_mask);
    else
      LogLine("cpu_mask=0x%llx result=fail errno=%d",(unsigned long long)Cfg.cpu_mask,errno);
  }
}

static void CollectRarFiles(const std::string &Root,std::vector<std::string> &Paths,int Depth=0)
{
  if (Depth>8 || Root==UNRAR_STAGE_DIR)
    return;

  DIR *Dir=opendir(Root.c_str());
  if (Dir==NULL)
    return;

  for (;;)
  {
    struct dirent *Entry=readdir(Dir);
    if (Entry==NULL)
      break;

    std::string Name=Entry->d_name;
    if (Name=="." || Name==".." || (!Name.empty() && Name[0]=='.'))
      continue;

    std::string FullPath=JoinPath(Root,Name);
    struct stat St;
    if (lstat(FullPath.c_str(),&St)!=0)
      continue;

    if (S_ISDIR(St.st_mode))
    {
      CollectRarFiles(FullPath,Paths,Depth+1);
      continue;
    }

    if (S_ISREG(St.st_mode) && EndsWithNoCase(Name,".rar"))
      Paths.push_back(FullPath);
  }
  closedir(Dir);
}

static bool FindFirstRar(const std::string &RarLocation,std::string &Path,std::string &Error)
{
  if (!IsDirPath(RarLocation))
  {
    Error="failed to open rar_location: "+RarLocation;
    return false;
  }

  std::vector<std::string> Paths;
  CollectRarFiles(RarLocation,Paths);

  if (Paths.empty())
  {
    Error="no .rar file found under "+RarLocation;
    return false;
  }

  std::sort(Paths.begin(),Paths.end());
  Path=Paths[0];
  return true;
}

static bool ResolveArchivePath(const PayloadConfig &Cfg,std::string &ArchivePath,std::string &Error)
{
  if (Cfg.filename.empty())
    return FindFirstRar(Cfg.rar_location,ArchivePath,Error);

  ArchivePath=Cfg.filename[0]=='/' ? Cfg.filename:JoinPath(Cfg.rar_location,Cfg.filename);
  if (!PathExists(ArchivePath))
  {
    Error="archive not found: "+ArchivePath;
    return false;
  }
  return true;
}

static bool RemoveTree(const std::string &Path,std::string &Error)
{
  struct stat St;
  if (lstat(Path.c_str(),&St)!=0)
    return errno==ENOENT;

  if (S_ISDIR(St.st_mode))
  {
    DIR *Dir=opendir(Path.c_str());
    if (Dir==NULL)
    {
      Error="failed to open directory: "+Path;
      return false;
    }
    for (;;)
    {
      struct dirent *Entry=readdir(Dir);
      if (Entry==NULL)
        break;
      std::string Name=Entry->d_name;
      if (Name=="." || Name=="..")
        continue;
      if (!RemoveTree(JoinPath(Path,Name),Error))
      {
        closedir(Dir);
        return false;
      }
    }
    closedir(Dir);
    if (rmdir(Path.c_str())!=0)
    {
      Error="failed to remove directory: "+Path;
      return false;
    }
    return true;
  }

  if (unlink(Path.c_str())!=0)
  {
    Error="failed to remove file: "+Path;
    return false;
  }
  return true;
}

static bool MovePath(const std::string &From,const std::string &To,std::string &Error)
{
  if (rename(From.c_str(),To.c_str())==0)
    return true;

  Error="failed to move "+From+" to "+To;
  return false;
}

static bool MoveDirectoryContents(const std::string &From,const std::string &To,std::string &Error)
{
  if (!MkdirAll(To))
  {
    Error="failed to create "+To;
    return false;
  }

  DIR *Dir=opendir(From.c_str());
  if (Dir==NULL)
  {
    Error="failed to open staging directory";
    return false;
  }

  for (;;)
  {
    struct dirent *Entry=readdir(Dir);
    if (Entry==NULL)
      break;
    std::string Name=Entry->d_name;
    if (Name=="." || Name=="..")
      continue;
    if (!MovePath(JoinPath(From,Name),JoinPath(To,Name),Error))
    {
      closedir(Dir);
      return false;
    }
  }
  closedir(Dir);
  return true;
}

static bool FindParamJson(const std::string &Root,std::string &ParamPath,int Depth=0)
{
  if (Depth>8)
    return false;

  std::string Candidate=JoinPath(Root,"sce_sys/param.json");
  if (PathExists(Candidate))
  {
    ParamPath=Candidate;
    return true;
  }

  DIR *Dir=opendir(Root.c_str());
  if (Dir==NULL)
    return false;

  for (;;)
  {
    struct dirent *Entry=readdir(Dir);
    if (Entry==NULL)
      break;
    std::string Name=Entry->d_name;
    if (Name=="." || Name=="..")
      continue;
    std::string Child=JoinPath(Root,Name);
    if (IsDirPath(Child) && FindParamJson(Child,ParamPath,Depth+1))
    {
      closedir(Dir);
      return true;
    }
  }
  closedir(Dir);
  return false;
}

static bool ExtractJsonString(const std::string &Json,const char *Key,std::string &Value)
{
  std::string Needle="\"";
  Needle+=Key;
  Needle+="\"";

  size_t Pos=Json.find(Needle);
  if (Pos==std::string::npos)
    return false;
  Pos=Json.find(':',Pos+Needle.size());
  if (Pos==std::string::npos)
    return false;
  Pos=Json.find('"',Pos+1);
  if (Pos==std::string::npos)
    return false;

  size_t End=Pos+1;
  std::string Out;
  while (End<Json.size())
  {
    char Ch=Json[End++];
    if (Ch=='\\' && End<Json.size())
    {
      Out+=Json[End++];
      continue;
    }
    if (Ch=='"')
    {
      Value=Out;
      return true;
    }
    Out+=Ch;
  }
  return false;
}

static bool ReadTitleId(const std::string &ParamPath,std::string &TitleId,std::string &Error)
{
  std::string Json;
  if (!ReadSmallFile(ParamPath,Json))
  {
    Error="failed to read "+ParamPath;
    return false;
  }

  if (ExtractJsonString(Json,"titleId",TitleId) ||
      ExtractJsonString(Json,"title_id",TitleId) ||
      ExtractJsonString(Json,"TITLE_ID",TitleId))
    return true;

  Error="TitleID not found in "+ParamPath;
  return false;
}

static const char *ExitReason(RAR_EXIT Code)
{
  switch(Code)
  {
    case RARX_SUCCESS: return "success";
    case RARX_WARNING: return "warning";
    case RARX_FATAL: return "fatal extraction error";
    case RARX_CRC: return "checksum or password error";
    case RARX_LOCK: return "archive lock error";
    case RARX_WRITE: return "write error";
    case RARX_OPEN: return "open error";
    case RARX_USERERROR: return "bad command or config";
    case RARX_MEMORY: return "out of memory";
    case RARX_CREATE: return "file create error";
    case RARX_NOFILES: return "no files extracted";
    case RARX_BADPWD: return "bad password";
    case RARX_READ: return "read error";
    case RARX_BADARC: return "bad archive";
    case RARX_USERBREAK: return "user break";
  }
  return "unknown error";
}

static int RunUnrarExtract(const std::string &ArchivePath,const std::string &DestPath,
                           const std::string &Password,uint Threads)
{
  ErrHandler.Clean();
  ErrHandler.SetSignalHandlers(true);

  std::unique_ptr<CommandData> Cmd(new CommandData);
  Cmd->Command=L"X";
  CharToWide(ArchivePath,Cmd->ArcName);
  CharToWide(DestPath,Cmd->ExtrPath);
  AddEndSlash(Cmd->ExtrPath);
  Cmd->AddArcName(Cmd->ArcName);
  Cmd->FileArgs.AddString(MASKALL);
  Cmd->AllYes=true;
  Cmd->Overwrite=OVERWRITE_ALL;
  Cmd->DisableCopyright=true;
  Cmd->DisableDone=true;
  Cmd->DisableNames=true;
  if (Threads>0)
    Cmd->Threads=std::min(Threads,MaxPoolThreads);

  if (!Password.empty())
  {
    std::wstring PasswordW;
    CharToWide(Password,PasswordW);
    Cmd->Password.Set(PasswordW.c_str());
  }

  uiInit(SOUND_NOTIFY_OFF);

  try
  {
    CmdExtract Extract(Cmd.get());
    Extract.DoExtract();
  }
  catch (RAR_EXIT ErrCode)
  {
    ErrHandler.SetErrorCode(ErrCode);
  }
  catch (std::bad_alloc&)
  {
    ErrHandler.SetErrorCode(RARX_MEMORY);
  }
  catch (std::length_error&)
  {
    ErrHandler.SetErrorCode(RARX_MEMORY);
  }
  catch (...)
  {
    ErrHandler.SetErrorCode(RARX_FATAL);
  }

  ErrHandler.MainExit=true;
  return ErrHandler.GetErrorCode();
}

static bool NormalizeExtractedApp(const PayloadConfig &Cfg,std::string &TitleId,
                                  std::string &FinalPath,std::string &Error)
{
  std::string ParamPath;
  if (!FindParamJson(UNRAR_STAGE_DIR,ParamPath))
  {
    Error="sce_sys/param.json not found after extraction";
    return false;
  }

  if (!ReadTitleId(ParamPath,TitleId,Error))
    return false;

  FinalPath=JoinPath(Cfg.extract_location,TitleId+"-app");
  std::string ExistingRoot=JoinPath(UNRAR_STAGE_DIR,TitleId+"-app");
  std::string ParamRoot=DirName(DirName(ParamPath));

  if (!MkdirAll(Cfg.extract_location))
  {
    Error="failed to create "+Cfg.extract_location;
    return false;
  }

  if (PathExists(FinalPath) && !RemoveTree(FinalPath,Error))
    return false;

  if (PathExists(ExistingRoot))
    return MovePath(ExistingRoot,FinalPath,Error);

  if (ParamRoot!=UNRAR_STAGE_DIR)
    return MovePath(ParamRoot,FinalPath,Error);

  return MoveDirectoryContents(UNRAR_STAGE_DIR,FinalPath,Error);
}

static std::string GetArchiveStem(const std::string &Name)
{
  std::string Stem=Name;
  std::string Lower=ToLowerAscii(Name);

  if (EndsWithNoCase(Stem,".rar"))
  {
    Stem.erase(Stem.size()-4);
    Lower.erase(Lower.size()-4);
  }

  size_t PartPos=Lower.rfind(".part");
  if (PartPos!=std::string::npos && PartPos+5<Lower.size())
  {
    bool PartNumber=true;
    for (size_t I=PartPos+5;I<Lower.size();I++)
      if (!std::isdigit((unsigned char)Lower[I]))
      {
        PartNumber=false;
        break;
      }
    if (PartNumber)
      Stem.erase(PartPos);
  }

  return Stem;
}

static bool IsArchivePartName(const std::string &Name,const std::string &Stem)
{
  std::string LowerName=ToLowerAscii(Name);
  std::string LowerStem=ToLowerAscii(Stem);

  if (LowerName==LowerStem+".rar")
    return true;

  if (LowerName.find(LowerStem+".part")==0 && EndsWithNoCase(LowerName,".rar"))
  {
    size_t DigitsStart=LowerStem.size()+5;
    size_t DigitsEnd=LowerName.size()-4;
    if (DigitsStart<DigitsEnd)
    {
      for (size_t I=DigitsStart;I<DigitsEnd;I++)
        if (!std::isdigit((unsigned char)LowerName[I]))
          return false;
      return true;
    }
  }

  if (LowerName.size()==LowerStem.size()+4 && LowerName.compare(0,LowerStem.size(),LowerStem)==0 &&
      LowerName[LowerStem.size()]=='.' && LowerName[LowerStem.size()+1]=='r' &&
      std::isdigit((unsigned char)LowerName[LowerStem.size()+2]) &&
      std::isdigit((unsigned char)LowerName[LowerStem.size()+3]))
    return true;

  return false;
}

static void DeleteArchiveFiles(const std::string &ArchivePath)
{
  std::string Dir=DirName(ArchivePath);
  std::string Name=BaseName(ArchivePath);
  std::string Stem=GetArchiveStem(Name);

  DIR *D=opendir(Dir.c_str());
  if (D==NULL)
    return;

  for (;;)
  {
    struct dirent *Entry=readdir(D);
    if (Entry==NULL)
      break;
    std::string Cur=Entry->d_name;
    if (Cur=="." || Cur=="..")
      continue;
    if (IsArchivePartName(Cur,Stem))
      unlink(JoinPath(Dir,Cur).c_str());
  }
  closedir(D);
}

int main(int argc,char *argv[])
{
#ifdef _UNIX
  setlocale(LC_ALL,"");
#endif

  if (!MkdirAll(UNRAR_DATA_DIR))
  {
    Notify("UnRAR error: failed to create " UNRAR_DATA_DIR);
    return 1;
  }

  LockGuard Lock;
  if (!Lock.Acquire())
  {
    Notify("UnRAR: extraction already running");
    LogLine("lock_error extraction already running");
    return 1;
  }

  PayloadConfig Cfg;
  std::string Error;
  if (!LoadConfig(Cfg,Error))
  {
    Notify("UnRAR error: %s",Error.c_str());
    LogLine("config_error %s",Error.c_str());
    return 1;
  }

  std::string ArchivePath;
  if (!ResolveArchivePath(Cfg,ArchivePath,Error))
  {
    Notify("UnRAR error: %s",Error.c_str());
    LogLine("archive_error %s",Error.c_str());
    return 1;
  }

  if (argc>1 && argv[1]!=NULL && argv[1][0]!=0)
    ArchivePath=argv[1];

  ApplySchedulingConfig(Cfg);

  Notify("UnRAR: starting extraction of %s",BaseName(ArchivePath).c_str());
  LogLine("start archive=%s rar_location=%s extract_location=%s delete_after=%u threads=%u nice=%d cpu_mask=0x%llx",
          ArchivePath.c_str(),Cfg.rar_location.c_str(),Cfg.extract_location.c_str(),
          Cfg.delete_after ? 1:0,Cfg.threads,Cfg.nice,(unsigned long long)Cfg.cpu_mask);
  LastNotifiedProgress=-10;

  if (!RemoveExistingInstallBeforeExtract(Cfg,ArchivePath,Error))
  {
    Notify("UnRAR error: %s",Error.c_str());
    LogLine("pre_remove_error %s",Error.c_str());
    return 1;
  }

  if (!RemoveTree(UNRAR_STAGE_DIR,Error))
  {
    Notify("UnRAR error: %s",Error.c_str());
    LogLine("staging_error %s",Error.c_str());
    return 1;
  }
  if (!MkdirAll(UNRAR_STAGE_DIR))
  {
    Notify("UnRAR error: failed to create staging directory");
    LogLine("staging_error failed to create staging directory");
    return 1;
  }

  uint64 ExtractStart=NowMs();
  int Code=RunUnrarExtract(ArchivePath,UNRAR_STAGE_DIR,Cfg.rar_password,Cfg.threads);
  uint64 ExtractMs=NowMs()-ExtractStart;
  LogLine("extract_result code=%d reason=%s elapsed_ms=%llu",
          Code,ExitReason((RAR_EXIT)Code),(unsigned long long)ExtractMs);
  if (Code!=RARX_SUCCESS && Code!=RARX_WARNING)
  {
    Notify("UnRAR error: %s",ExitReason((RAR_EXIT)Code));
    return Code;
  }
  Ps5UnrarProgress(100);

  std::string TitleId;
  std::string FinalPath;
  uint64 NormalizeStart=NowMs();
  if (!NormalizeExtractedApp(Cfg,TitleId,FinalPath,Error))
  {
    Notify("UnRAR error: %s",Error.c_str());
    LogLine("normalize_error %s",Error.c_str());
    return 1;
  }
  uint64 NormalizeMs=NowMs()-NormalizeStart;

  RemoveTree(UNRAR_STAGE_DIR,Error);

  if (Cfg.delete_after)
    DeleteArchiveFiles(ArchivePath);

  Notify("UnRAR done: %s installed to %s",TitleId.c_str(),FinalPath.c_str());
  LogLine("done title_id=%s final_path=%s extract_ms=%llu normalize_ms=%llu",
          TitleId.c_str(),FinalPath.c_str(),(unsigned long long)ExtractMs,
          (unsigned long long)NormalizeMs);
  return Code;
}
