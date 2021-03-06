#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>

#include "App.h"
#include <NodeMonitor.h>
#include <Path.h>
#include <String.h>
#include <File.h>

const char * local_path_string = "/boot/home/Dropbox/";
const char * local_path_string_noslash = "/boot/home/Dropbox";
const int32 MY_DELTA_CONST = 'DBDL';
const bigtime_t HOW_OFTEN_TO_POLL = 10000000;

// String modification helper functions

/*
* Convert a Dropbox path to a local absolute filepath
* by adding the <path to Dropbox> to the beginning
*/
BString db_to_local_filepath(const char * local_path)
{
  BString s;
  s << local_path_string << local_path;
  return s;
}

/*
* Convert a local absolute filepath to a Dropbox one
* by removing the <path to Dropbox> from the beginning
*/
BString local_to_db_filepath(const char * local_path)
{
  BString s;
  s = BString(local_path);
  s.RemoveFirst(local_path_string);
  return s;
}

/*
* Moves the first line in src to dest.
* Destructively alters src.
* newline is included in the move.
*/
int32
get_next_line(BString *src, BString *dest)
{
  const int32 eol = src->FindFirst('\n');
  if(eol == B_ERROR)
    return B_ERROR;

  src->MoveInto(*dest,0,eol+1);
  return B_OK;
}

/*
* Runs a python script given the name and args
*
* Takes an array of strings and its length.
* The strings must be null terminated.
*/
BString *
run_python_script(char * argv[],int length)
{
  BString *output = new BString;
  char buf[BUFSIZ];

  int fd[2];
  pipe(fd);
  pid_t pid = fork();

  if(pid < 0)
    return output; //error
  if(pid == 0)
  {
    close(fd[0]);
    dup2(fd[1],STDOUT_FILENO);

    char * real_argv[length + 2];
    real_argv[0] = "python";
    real_argv[length+1] = NULL;
    for(int i=0; i<length;i++)
      real_argv[i+1]=argv[i];

    execvp("python",real_argv);
  }
  else //parent
  {
    close(fd[1]);
    dup2(fd[0],STDIN_FILENO);

    int status;
    waitpid(pid, &status, 0);

    int len = read(fd[0],buf,BUFSIZ);
    while(len > 0)
    {
      output->Append(buf,len);
      len = read(fd[0],buf,BUFSIZ);
    } 
  }
  printf("output:|%s|\n",output->String());
  return output;
}

// Talk to Dropbox

/*
* Given a local file path,
* call the script to delete the corresponding Dropbox file
*/
void
delete_file_on_dropbox(const char * filepath)
{
  printf("Telling Dropbox to Delete: %s\n",local_to_db_filepath(filepath).String());
  char * argv[2];
  argv[0] = "db_rm.py";
  BString db_filepath = local_to_db_filepath(filepath);
  const char * tmp = db_filepath.String();
  char not_const[db_filepath.CountChars()];
  strcpy(not_const,tmp);
  argv[1] = not_const;
  run_python_script(argv,2);
}

/*
* Given the local file path of a new file,
* run the script to upload it to Dropbox
*/
BString *
add_file_to_dropbox(const char * filepath)
{
  //return get_or_put("db_put.py",filepath, local_to_db_filepath(filepath));
  char * argv[3];
  argv[0] = "db_put.py";

  BString db_filepath = local_to_db_filepath(filepath);
  const char * tmp = db_filepath.String();
  char not_const[db_filepath.CountChars()];
  strcpy(not_const,tmp);
  argv[2] = not_const;

  char not_const2[strlen(filepath)];
  strcpy(not_const2,filepath);
  argv[1] = not_const2;

  BString *result = run_python_script(argv,3);
  result->RemoveAll("\n"); //trim trailing new lines
  return result;
}

/*
* Given the local file path of a new folder,
* run the script to mkdir on Dropbox
*/
void
add_folder_to_dropbox(const char * filepath)
{
  //one_path_arg("db_mkdir.py",local_to_db_filepath(filepath));
  char * argv[2];
  argv[0] = "db_mkdir.py";

  char not_const[strlen(filepath)];
  strcpy(not_const,filepath);
  argv[1] = not_const;

  run_python_script(argv,2);
}

/*
* For use with the return value of db_put.py
*  Get the real Dropbox path of the pushed file.
*/
BString*
parse_path(const BString *result)
{
  BString *path = new BString;
  int32 eol = result->FindFirst('\n');
  int32 last_space_in_first_line;
  if(eol != B_ERROR){
    last_space_in_first_line = result->FindLast(' ',eol);
  } else {
    last_space_in_first_line = result->FindLast(' ');
  }
  result->CopyInto(*path,0,last_space_in_first_line);
  return path;
}

/*
* For use with the return value of db_put.py
*  Get the parent_rev of the pushed file.
*/
BString*
parse_parent_rev(const BString *result)
{
  BString *parent_rev = new BString;
  int32 eol = result->FindFirst('\n');
  int32 last_space_in_first_line;
  if(eol != B_ERROR){
    last_space_in_first_line = result->FindLast(' ',eol);
  } else {
    last_space_in_first_line = result->FindLast(' ');
  }
  result->CopyInto(*parent_rev,last_space_in_first_line + 1, eol - last_space_in_first_line - 1);
  return parent_rev;
}

/*
* Given the BNode of a local file,
* return the parent_rev as stored in an attribute
*/
BString *
get_parent_rev(BNode *node)
{
  int32 len;
  ssize_t bytes = node->ReadAttr("parent_rev_len",B_INT32_TYPE,0,(void*)&len,4);
  if(bytes != 4) {
   printf("tried to read parent_rev_len, but only read %d bytes\n",bytes);
   BString * empty = new BString();
   return empty;
  }
  char str[len];
  bytes = node->ReadAttr("parent_rev",B_STRING_TYPE, 0, (void*)str, len);
  if(bytes == 0) printf("tried and failed to read parent_rev");
  BString * parent_rev = new BString(str);
  return parent_rev;
}

/*
* Store the parent_rev as an attribute on a local file
* Takes the BNode representing the file
* and a BString containing the parent_rev
*/
void
set_parent_rev(BNode *node, const BString *rev)
{
  printf("setting parent_rev |%s| of len %d\n"
        , rev->String()
        , rev->CountChars() + 1);
  node_ref nref;
  node->GetNodeRef(&nref);
  watch_node(&nref, B_STOP_WATCHING, be_app_messenger);

  int32 len = rev->CountChars() + 1;
  const char * str = rev->String();
  node->WriteAttr("parent_rev_len"
                , B_INT32_TYPE
                , 0
                , (void*)&len
                , 4);
  node->WriteAttr("parent_rev"
                , B_STRING_TYPE
                , 0
                , (void*)str
                , len);

  watch_node(&nref, B_WATCH_STAT, be_app_messenger);
}

/*
* Given a local file path,
* update the corresponding file on Dropbox
*/
void
update_file_in_dropbox(const char * filepath, const char *parent_rev)
{
  char * argv[4];
  argv[0] = "db_put.py";

  BString db_filepath = local_to_db_filepath(filepath);
  const char * tmp = db_filepath.String();
  char not_const[db_filepath.CountChars()];
  strcpy(not_const,tmp);
  argv[2] = not_const;

  char not_const2[strlen(filepath)];
  strcpy(not_const2,filepath);
  argv[1] = not_const2;

  char not_const3[strlen(parent_rev)];
  strcpy(not_const3,parent_rev);
  argv[3] = not_const3;

  BString *result = run_python_script(argv,4);
  BString *real_path = parse_path(result);
  BString *new_parent_rev = parse_parent_rev(result);
  delete result;

  printf("path:|%s|\nparent_rev:|%s|\n",real_path->String(),new_parent_rev->String());

  BNode node = BNode(filepath);
  set_parent_rev(&node,new_parent_rev);
  delete new_parent_rev;

  BEntry entry = BEntry(filepath);
  BPath old_path;
  entry.GetPath(&old_path);

  BPath new_path = BPath(db_to_local_filepath(real_path->String()).String());

  printf("Should I move %s to %s?\n", old_path.Path(), new_path.Path());
  if(strcmp(new_path.Leaf(),old_path.Leaf()) != 0)
  {
    printf("moving %s to %s\n", old_path.Leaf(), new_path.Leaf());
    BEntry entry = BEntry(old_path.Path()); //entry for local path
    status_t err = entry.Rename(new_path.Leaf(),true);
    if(err != B_OK) printf("error moving: %s\n",strerror(err));
  }
  delete real_path;
}

//Local filesystem stuff

//TODO: pick better default permissions...
void
create_local_directory(BString *dropbox_path)
{
    BString *local_path = new BString(local_path_string);
    local_path->Append(*dropbox_path);
    status_t err = create_directory(local_path->String(), 0x0777);
    printf("Create local dir %s: %s\n",local_path->String(),strerror(err));
    delete local_path;
}

/*
* Subscribe to Node Monitor alerts
* Just wraps the watch_node function of the Node Monitor
* (with the BMessenger being be_app_messenger)
*/
void
watch_entry(const BEntry *entry, int flag)
{
  node_ref nref;
  status_t err;

  err = entry->GetNodeRef(&nref);
  if(err == B_OK)
  {
    err = watch_node(&nref, flag, be_app_messenger);
    if(err != B_OK)
      printf("watch_entry: Not Ok.\n");
  }
}

/*
* Given a BEntry* representing a file (or folder)
* add the relevant BFile and BPath to the global tracking lists
* (tracked_files and tracked_filepaths)
*/
void
App::track_file(BEntry *new_file)
{
  BFile *file = new BFile(new_file, B_READ_ONLY);
  this->tracked_files.AddItem((void*)file);
  BPath *path = new BPath;
  new_file->GetPath(path);
  this->tracked_filepaths.AddItem((void*)path);
}

/*
* Given a directory, subscribe to Node Monitor
* messages on it and all it's descendents.
* For all directories, use B_WATCH_DIRECTORY.
* For all file, use B_WATCH_STAT.
* It also watches the directory itself
* with B_WATCH_DIRECOTRY.
*/
void
App::recursive_watch(BDirectory *dir)
{
  status_t err;

  BEntry entry;
  err = dir->GetNextEntry(&entry);

  //for each file in the current directory
  while(err == B_OK)
  {
    //put this file in global list
    this->track_file(&entry);
    BFile file = BFile(&entry,B_READ_ONLY);
    if(file.IsDirectory())
    {
      watch_entry(&entry,B_WATCH_DIRECTORY);
      BDirectory *ndir = new BDirectory(&entry);
      this->recursive_watch(ndir);
      delete ndir;
    }
    else
    {
      watch_entry(&entry,B_WATCH_STAT);
    }

    err = dir->GetNextEntry(&entry);
  }
}

/*
* Given a local directory, do the equivalent of `rm -rf`
* on it, but using the actual filesystem API.
* You can't just use dir->Remove() because that
* gives an error if the directory is not empty.
*/
void
rm_rf(BDirectory *dir)
{
  status_t err;
  BEntry entry;
  err = dir->GetNextEntry(&entry);
  while(err==B_OK)
  {
    BFile file = BFile(&entry, B_READ_ONLY);
    if(file.IsDirectory())
    {
      BDirectory ndir = BDirectory(&entry);
      rm_rf(&ndir);
    }

    err = entry.Remove();
    if(err != B_OK)
    {
      BPath path;
      entry.GetPath(&path);
      printf("Remove Error: %s on %s\n",strerror(err),path.Path());
      //what to do if I can't remove something?
    }

    err = dir->GetNextEntry(&entry);
  }
  err = dir->GetEntry(&entry);
  err = entry.Remove();
  if(err != B_OK)
    printf("Folder Removal Error: %s\n", strerror(err));
}

/*
* Find the index of the target in the tracked_files list.
* Returns -1 if target is not in the list.
*/
int32
App::find_nref_in_tracked_files(node_ref target)
{
  node_ref current_nref;
  BFile * current_file;
  int32 ktr = 0;
  int32 limit = this->tracked_files.CountItems();

  while((current_file = (BFile *)this->tracked_files.ItemAt(ktr++)) && ktr<=limit)
  {
    current_file->GetNodeRef(&current_nref);
    if(target == current_nref)
    {
      return --ktr; //account for ++ in while
    }
  }
  return -1;
}

bool
check_exists(BString db_path) {
  BString local_path = db_to_local_filepath(db_path);
  BEntry entry = BEntry(local_path.String());
  bool init = entry.InitCheck() == B_OK;
  bool exists = entry.Exists();
  printf("init:%d,exists:%d\n",init,exists);
  return init && exists;
}

bool
exists(BPath *path)
{
  BEntry entry = BEntry(path->Path());
  return (entry.InitCheck() == B_OK) && entry.Exists();
}

BPath*
find_existing_subpath(BPath *fullpath) {
  if(exists(fullpath)) return new BPath(*fullpath);
  BPath *current;
  BPath previous = BPath(*fullpath);
  previous.GetParent(current);
  while(!(exists(current))) {
    current->GetParent(&previous);
    BPath *tmp = &previous;
    previous = *current;
    current = tmp;
  }
  return new BPath(previous);
}

// Act on Deltas
/*
* Given a single line of the output of db_delta.py
* Figures out what to do and does it.
* (adds and removes files and directories)
*/
int
App::parse_command(BString command)
{
  command.RemoveAll("\n"); //remove trailing whitespace
  if(command.Compare("RESET") == 0)
  {
    printf("Burn Everything. 8D\n");

    status_t err = stop_watching(be_app_messenger);
    if(err != B_OK) printf("stop_watching error: %s\n",strerror(err));

    BDirectory dir = BDirectory(local_path_string);
    rm_rf(&dir);

    BString str = BString("/"); //create_local_path wants a remote path 
    create_local_directory(&str);

    this->recursive_watch(&dir);
  }
  else if(command.Compare("FILE ",5) == 0)
  {
    BString path, dirpath, partial_path;
    BPath *bpath;
    int32 last_space = command.FindLast(" ");
    command.CopyInto(path,5,last_space - 5);

    path.CopyInto(dirpath,0,path.FindLast("/"));

    create_local_directory(&dirpath);
    //TODO fix watching new dirs
    bpath = new BPath(db_to_local_filepath(path.String()).String());
    BEntry new_file = BEntry(bpath->Path());
    if(new_file.InitCheck() && new_file.Exists()) {
      this->new_paths.AddItem((void*)bpath);
    } else {
      this->edited_paths.AddItem((void*)bpath);
    }

    printf("create a file at |%s|\n",path.String());
    char *argv[3];
    argv[0] = "db_get.py";
    char not_const1[path.CountChars() + 1];
    strcpy(not_const1,path.String());
    argv[1] = not_const1;
    BString tmp = db_to_local_filepath(path.String());
    char not_const2[tmp.CountChars() + 1]; //plus one for null
    strcpy(not_const2,tmp.String());
    argv[2] = not_const2;

    //create/update file
    //potential problem: takes awhile to do this step
    // having watching for dir turned off is risky.    
    BString * b = run_python_script(argv,3);
    delete b;

    //start watching the new/updated file
    node_ref nref;
    new_file = BEntry(db_to_local_filepath(path.String()).String());
    new_file.GetNodeRef(&nref);
    status_t err = watch_node(&nref,B_WATCH_STAT,be_app_messenger);

    BString parent_rev;
    command.CopyInto(parent_rev,last_space + 1, command.CountChars() - (last_space+1));
    BNode node = BNode(db_to_local_filepath(path.String()).String());
    set_parent_rev(&node,&parent_rev);
  }
  else if(command.Compare("FOLDER ",7) == 0)
  {
    BString path;
    command.CopyInto(path,7,command.FindLast(" ") - 7);

    //ignore the creation message
    BPath bpath = BPath(db_to_local_filepath(path.String()).String());
    BPath *actually_exists = find_existing_subpath(&bpath);
    this->new_paths.AddItem((void*)actually_exists);

    //create all nescessary dirs in path
    printf("create a folder at |%s|\n", path.String());
    create_local_directory(&path);

    //start watching the new dir
    BDirectory existing_dir = BDirectory(actually_exists->Path());
    recursive_watch(&existing_dir);
  }
  else if(command.Compare("REMOVE ",7) == 0)
  {
    //TODO: deal with Dropbox file paths being case-insensitive
    //which here means all lower case
    BString path;
    command.CopyInto(path,7,command.Length() - 7);

    const char * pathstr = db_to_local_filepath(path.String()).String();
    BPath *bpath = new BPath(pathstr);
    //TODO: check if it exists...
    this->removed_paths.AddItem((void*)bpath);

    printf("Remove whatever is at |%s|\n", pathstr);

    BEntry entry = BEntry(pathstr);
    status_t err = entry.Remove();
    if(err != B_OK)
      printf("Removal error: %s\n", strerror(err));
  }
  else
  {
    printf("Did not recognize command.\n");
    return B_ERROR;
  }
  return B_OK;
}

/*
* wrapper for calling db_deltas.py
* and then running parse_command on each line
* of the output.
*/
void
App::pull_and_apply_deltas()
{
  char *argv[1];
  argv[0] = "db_delta.py";
  BString *delta_commands = run_python_script(argv,1);
  BString line;
  printf("*************RUNNING DELTA\n");
  while(get_next_line(delta_commands,&line) == B_OK)
  {
    int x = parse_command(line); 
    if(x != B_OK)
      break;
  }
  printf("*************RAN DELTA\n");
}

/*
* Sets up the Node Monitoring for Dropbox folder and contents
* and creates data structure for determining which files are deleted or edited
*/
App::App(void)
  : BApplication("application/x-vnd.lh-MyDropboxClient")
{
  pull_and_apply_deltas();
  printf("Done pulling changes, now to start tracking\n");

  //start watching ~/Dropbox folder contents (create, delete, move)
  BDirectory dir(local_path_string_noslash); //don't use ~ here
  node_ref nref;
  status_t err;
  if(dir.InitCheck() == B_OK){
    dir.GetNodeRef(&nref);
    err = watch_node(&nref, B_WATCH_DIRECTORY, be_app_messenger);
    if(err != B_OK)
      printf("Watch Node: Not OK\n");
  }

  printf("Done watching root directory\n");

  //watch all the child files for edits and the folders for create/delete/move
  this->recursive_watch(&dir);

  printf("Done watching and tracking all children of ~/Dropbox.\n");
  BMessage msg = BMessage(MY_DELTA_CONST);
  bigtime_t microseconds = HOW_OFTEN_TO_POLL;
  this->msg_runner = new BMessageRunner(be_app_messenger, msg, microseconds, -1);
}

bool
App::ignore_removed(BPath *path)
{
  printf("In ignore_removed\n");
  //find matching path object in list (by value comparison)
  for(int32 i = 0; i < this->removed_paths.CountItems(); i++) {
    BPath *current = (BPath*)this->removed_paths.ItemAt(i);
    printf("removed_paths[%d]=%s\n",i,current->Path());
    if( *current == *path) {
      BPath *p = (BPath*)this->removed_paths.RemoveItem(i);
      delete p;
      printf("\treturning true\n");
      return true;
    }
  }
  printf("\treturning false\n");
  return false;
}

bool
App::ignore_created(BPath *path)
{
  for(int32 i = 0; i < this->new_paths.CountItems(); i++) {
    BPath *current = (BPath*)this->new_paths.ItemAt(i);
    if( *current == *path) {
      BPath *p = (BPath*)this->new_paths.RemoveItem(i);
      delete p;
      return true;
    }
  }
  return false;
}

bool
App::ignore_edited(BPath *path)
{
  for(int32 i = 0; i < this->edited_paths.CountItems(); i++) {
    BPath *current = (BPath*)this->edited_paths.ItemAt(i);
    if( *current == *path) {
      BPath *p = (BPath*)this->edited_paths.RemoveItem(i);
      delete p;
      return true;
    }
  }
  return false;
}

/*
* Message Handling Function
* If it's a node monitor message,
* then figure out what to do based on it.
* Otherwise, let BApplication handle it.
*/
void
App::MessageReceived(BMessage *msg)
{
  printf("message received:\n");
  msg->PrintToStream();
  switch(msg->what)
  {
    case MY_DELTA_CONST:
    {
      printf("Pulling changes from Dropbox\n");
      pull_and_apply_deltas();
      break;
    }
    case B_NODE_MONITOR:
    {
      printf("Received Node Monitor Alert\n");
      status_t err;
      int32 opcode;
      err = msg->FindInt32("opcode",&opcode);
      if(err == B_OK)
      {
        switch(opcode)
        {
          case B_ENTRY_CREATED:
          {
            printf("CREATED NEW FILE\n");
            entry_ref ref;
            BPath path;
            const char * name;

            // unpack the message
            msg->FindInt32("device",&ref.device);
            msg->FindInt64("directory",&ref.directory);
            msg->FindString("name",&name);
            ref.set_name(name);

            BEntry new_file = BEntry(&ref);
            new_file.GetPath(&path);


            //if we said to ignore a `NEW` msg from the path, then ignore it
            if(this->ignore_created(&path)) return;

            this->track_file(&new_file);

            if(new_file.IsDirectory())
            {
               add_folder_to_dropbox(path.Path());
               BDirectory new_dir = BDirectory(&new_file);
               this->recursive_watch(&new_dir);
            }
            else
            {
              BString *result = add_file_to_dropbox(path.Path());
              BString *real_path = parse_path(result);
              BString *parent_rev = parse_parent_rev(result);
              delete result;

              printf("path:|%s|\nparent_rev:|%s|\n",real_path->String(),parent_rev->String());

              BNode node = BNode(&new_file);
              set_parent_rev(&node,parent_rev);
              delete parent_rev;
              BPath new_path = BPath(db_to_local_filepath(real_path->String()).String());

              if(strcmp(new_path.Leaf(),path.Leaf()) != 0)
              {
                printf("moving %s to %s\n", path.Leaf(), new_path.Leaf());
                BEntry entry = BEntry(path.Path()); //entry for local path
                status_t err = entry.Rename(new_path.Leaf(),true);
                if(err != B_OK) printf("error moving: %s\n",strerror(err));
              }
              
              delete real_path;
              watch_entry(&new_file,B_WATCH_STAT);
            }
            break;
          }
          case B_ENTRY_MOVED:
          {
            printf("MOVED FILE\n");
            entry_ref eref;
            BDirectory from_dir, to_dir;
            node_ref from_ref,to_ref,nref;
            BPath path;
            const char* name;

            msg->FindInt32("device",&from_ref.device);
            msg->FindInt32("device",&to_ref.device);
            msg->FindInt32("device",&eref.device);
            msg->FindInt32("device",&nref.device);

            msg->FindInt64("from directory",&from_ref.node);
            msg->FindInt64("to directory",&to_ref.node);
            msg->FindInt64("to directory",&eref.directory);

            msg->FindInt64("node",&nref.node);

            msg->FindString("name",&name);
            eref.set_name(name);

            err = from_dir.SetTo(&from_ref);
            err = to_dir.SetTo(&to_ref);

            BEntry dest_entry = BEntry(&eref);
            BEntry test = BEntry("/boot/home/Dropbox/hi");
            BDirectory dropbox_local = BDirectory(local_path_string);
            bool into_dropbox = dropbox_local.Contains(&dest_entry);
            int32 index = this->find_nref_in_tracked_files(nref);
            if((index >= 0) && into_dropbox)
            {
              printf("moving within dropbox\n");
              BPath *old_path = (BPath*)this->tracked_filepaths.ItemAt(index);
              BPath new_path;
              dest_entry.GetPath(&new_path);

              char *argv[3];
              argv[0] = "db_mv.py";
              BString opath = local_to_db_filepath(old_path->Path());
              BString npath = local_to_db_filepath(new_path.Path());
              char not_const_o[opath.CountChars()];
              char not_const_n[npath.CountChars()];
              strcpy(not_const_o,opath.String());
              strcpy(not_const_n,npath.String());
              argv[1] = not_const_o;
              argv[2] = not_const_n;
              run_python_script(argv,3);

              old_path->SetTo(&dest_entry);
            }
            else if(index >= 0)
            {
              printf("moving the file out of dropbox\n");
              BPath *old_path = (BPath*)this->tracked_filepaths.ItemAt(index);
              delete_file_on_dropbox(old_path->Path());
              this->tracked_files.RemoveItem(index);
              this->tracked_filepaths.RemoveItem(index);
            }
            else if(into_dropbox)
            {
              printf("moving file into dropbox\n");
              BPath new_path;
              dest_entry.GetPath(&new_path);

              this->track_file(&dest_entry);

              if(dest_entry.IsDirectory())
              {
                 add_folder_to_dropbox(new_path.Path());
                 BDirectory new_dir = BDirectory(&dest_entry);
                 this->recursive_watch(&new_dir);
              }
              else
              {
                add_file_to_dropbox(new_path.Path());
                watch_entry(&dest_entry,B_WATCH_STAT);
              }
            }
            else
            {
              printf("moving unrelated file...?\n");
            }

            break;
          }
          case B_ENTRY_REMOVED:
          {
            printf("DELETED FILE\n");
            node_ref nref;
            msg->FindInt32("device", &nref.device);
            msg->FindInt64("node", &nref.node);

            int32 index = this->find_nref_in_tracked_files(nref);
            if(index >= 0)
            {
              BPath *path = (BPath*)this->tracked_filepaths.ItemAt(index);
              printf("local file %s deleted\n",path->Path());
              
              if(ignore_removed(path)) return;

              delete_file_on_dropbox(path->Path());
              this->tracked_files.RemoveItem(index);
              this->tracked_filepaths.RemoveItem(index);
            }
            else
            {
              printf("could not find deleted file\n");
            }

            break;
          }
          case B_STAT_CHANGED:
          {
            printf("EDITED FILE\n");
            node_ref nref;
            msg->FindInt32("device", &nref.device);
            msg->FindInt64("node", &nref.node);

            int32 index = this->find_nref_in_tracked_files(nref);
            if(index >= 0)
            {
              BPath *path = (BPath*)this->tracked_filepaths.ItemAt(index);
              if(ignore_edited(path)) return;
              BNode node = BNode(path->Path());
              BString * rev = get_parent_rev(&node);
              printf("parent_rev:|%s|\n",rev->String());
              
              update_file_in_dropbox(path->Path(),rev->String());
            }
            else
            {
              printf("Could not find edited file\n");
            }
            break;
          }
          default:
          {
            printf("default case opcode...\n");
          }
        }
      }
      break;
    }
    default:
    {
      BApplication::MessageReceived(msg);
      break;
    }
  }
}

int
main(void)
{
  //set up application (watch Dropbox folder & contents)
  App *app = new App();

  //start the application
  app->Run();

  //clean up now that we're shutting down
  delete app;
  return 0;
}
