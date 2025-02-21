/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Spencer Dixon <spencercdixon@gmail.com>
 * Copyright (c) 2021, Liav A. <liavalb@hotmail.co.il>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Singleton.h>
#include <Kernel/Debug.h>
#include <Kernel/FileSystem/Custody.h>
#include <Kernel/FileSystem/FileDescription.h>
#include <Kernel/FileSystem/ProcFS.h>
#include <Kernel/FileSystem/VirtualFileSystem.h>
#include <Kernel/Heap/kmalloc.h>
#include <Kernel/Process.h>
#include <Kernel/Sections.h>
#include <LibC/errno_numbers.h>

namespace Kernel {

static Singleton<ProcFSComponentRegistry> s_the;

ProcFSComponentRegistry& ProcFSComponentRegistry::the()
{
    return *s_the;
}

UNMAP_AFTER_INIT void ProcFSComponentRegistry::initialize()
{
    VERIFY(!s_the.is_initialized());
    s_the.ensure_instance();
}

UNMAP_AFTER_INIT ProcFSComponentRegistry::ProcFSComponentRegistry()
    : m_root_directory(ProcFSRootDirectory::must_create())
{
}

RefPtr<ProcFS> ProcFS::create()
{
    return adopt_ref_if_nonnull(new (nothrow) ProcFS);
}

ProcFS::ProcFS()
    : m_root_inode(ProcFSComponentRegistry::the().root_directory().to_inode(*this))
{
}

ProcFS::~ProcFS()
{
}

bool ProcFS::initialize()
{
    return true;
}

Inode& ProcFS::root_inode()
{
    return *m_root_inode;
}

ProcFSInode::ProcFSInode(const ProcFS& fs, InodeIndex index)
    : Inode(const_cast<ProcFS&>(fs), index)
{
}

ProcFSInode::~ProcFSInode()
{
}

void ProcFSInode::flush_metadata()
{
}

KResult ProcFSInode::add_child(Inode&, const StringView&, mode_t)
{
    return EROFS;
}

KResultOr<NonnullRefPtr<Inode>> ProcFSInode::create_child(StringView, mode_t, dev_t, uid_t, gid_t)
{
    return EROFS;
}

KResult ProcFSInode::remove_child(const StringView&)
{
    return EROFS;
}

KResult ProcFSInode::chmod(mode_t)
{
    return EPERM;
}

KResult ProcFSInode::chown(uid_t, gid_t)
{
    return EPERM;
}

KResult ProcFSInode::truncate(u64)
{
    return EPERM;
}

NonnullRefPtr<ProcFSGlobalInode> ProcFSGlobalInode::create(const ProcFS& fs, const ProcFSExposedComponent& component)
{
    return adopt_ref(*new (nothrow) ProcFSGlobalInode(fs, component));
}

ProcFSGlobalInode::ProcFSGlobalInode(const ProcFS& fs, const ProcFSExposedComponent& component)
    : ProcFSInode(fs, component.component_index())
    , m_associated_component(component)
{
}

void ProcFSGlobalInode::did_seek(FileDescription& description, off_t new_offset)
{
    if (new_offset != 0)
        return;
    auto result = m_associated_component->refresh_data(description);
    if (result.is_error()) {
        // Subsequent calls to read will return EIO!
        dbgln("ProcFS: Could not refresh contents: {}", result.error());
    }
}

KResult ProcFSGlobalInode::attach(FileDescription& description)
{
    return m_associated_component->refresh_data(description);
}

KResultOr<size_t> ProcFSGlobalInode::read_bytes(off_t offset, size_t count, UserOrKernelBuffer& buffer, FileDescription* fd) const
{
    return m_associated_component->read_bytes(offset, count, buffer, fd);
}

StringView ProcFSGlobalInode::name() const
{
    return m_associated_component->name();
}

KResult ProcFSGlobalInode::traverse_as_directory(Function<bool(FileSystem::DirectoryEntryView const&)>) const
{
    VERIFY_NOT_REACHED();
}

RefPtr<Inode> ProcFSGlobalInode::lookup(StringView)
{
    VERIFY_NOT_REACHED();
}

InodeMetadata ProcFSGlobalInode::metadata() const
{
    MutexLocker locker(m_inode_lock);
    InodeMetadata metadata;
    metadata.inode = { fsid(), m_associated_component->component_index() };
    metadata.mode = S_IFREG | m_associated_component->required_mode();
    metadata.uid = m_associated_component->owner_user();
    metadata.gid = m_associated_component->owner_group();
    metadata.size = m_associated_component->size();
    metadata.mtime = m_associated_component->modified_time();
    return metadata;
}

KResultOr<size_t> ProcFSGlobalInode::write_bytes(off_t offset, size_t count, const UserOrKernelBuffer& buffer, FileDescription* fd)
{
    return m_associated_component->write_bytes(offset, count, buffer, fd);
}

NonnullRefPtr<ProcFSDirectoryInode> ProcFSDirectoryInode::create(const ProcFS& procfs, const ProcFSExposedComponent& component)
{
    return adopt_ref(*new (nothrow) ProcFSDirectoryInode(procfs, component));
}

ProcFSDirectoryInode::ProcFSDirectoryInode(const ProcFS& fs, const ProcFSExposedComponent& component)
    : ProcFSGlobalInode(fs, component)
{
}

ProcFSDirectoryInode::~ProcFSDirectoryInode()
{
}
InodeMetadata ProcFSDirectoryInode::metadata() const
{
    MutexLocker locker(m_inode_lock);
    InodeMetadata metadata;
    metadata.inode = { fsid(), m_associated_component->component_index() };
    metadata.mode = S_IFDIR | m_associated_component->required_mode();
    metadata.uid = m_associated_component->owner_user();
    metadata.gid = m_associated_component->owner_group();
    metadata.size = 0;
    metadata.mtime = m_associated_component->modified_time();
    return metadata;
}
KResult ProcFSDirectoryInode::traverse_as_directory(Function<bool(FileSystem::DirectoryEntryView const&)> callback) const
{
    MutexLocker locker(procfs().m_lock);
    return m_associated_component->traverse_as_directory(procfs().fsid(), move(callback));
}

RefPtr<Inode> ProcFSDirectoryInode::lookup(StringView name)
{
    MutexLocker locker(procfs().m_lock);
    auto component = m_associated_component->lookup(name);
    if (!component)
        return {};
    return component->to_inode(procfs());
}

NonnullRefPtr<ProcFSLinkInode> ProcFSLinkInode::create(const ProcFS& procfs, const ProcFSExposedComponent& component)
{
    return adopt_ref(*new (nothrow) ProcFSLinkInode(procfs, component));
}

ProcFSLinkInode::ProcFSLinkInode(const ProcFS& fs, const ProcFSExposedComponent& component)
    : ProcFSGlobalInode(fs, component)
{
}

InodeMetadata ProcFSLinkInode::metadata() const
{
    MutexLocker locker(m_inode_lock);
    InodeMetadata metadata;
    metadata.inode = { fsid(), m_associated_component->component_index() };
    metadata.mode = S_IFLNK | m_associated_component->required_mode();
    metadata.uid = m_associated_component->owner_user();
    metadata.gid = m_associated_component->owner_group();
    metadata.size = 0;
    metadata.mtime = m_associated_component->modified_time();
    return metadata;
}

ProcFSProcessAssociatedInode::ProcFSProcessAssociatedInode(const ProcFS& fs, ProcessID associated_pid, InodeIndex determined_index)
    : ProcFSInode(fs, determined_index)
    , m_pid(associated_pid)
{
}

KResultOr<size_t> ProcFSProcessAssociatedInode::write_bytes(off_t, size_t, const UserOrKernelBuffer&, FileDescription*)
{
    VERIFY_NOT_REACHED();
}

NonnullRefPtr<ProcFSProcessDirectoryInode> ProcFSProcessDirectoryInode::create(const ProcFS& procfs, ProcessID pid)
{
    return adopt_ref_if_nonnull(new (nothrow) ProcFSProcessDirectoryInode(procfs, pid)).release_nonnull();
}

ProcFSProcessDirectoryInode::ProcFSProcessDirectoryInode(const ProcFS& procfs, ProcessID pid)
    : ProcFSProcessAssociatedInode(procfs, pid, SegmentedProcFSIndex::build_segmented_index_for_pid_directory(pid))
{
}

KResult ProcFSProcessDirectoryInode::attach(FileDescription&)
{
    return KSuccess;
}

InodeMetadata ProcFSProcessDirectoryInode::metadata() const
{
    MutexLocker locker(m_inode_lock);
    auto process = Process::from_pid(associated_pid());
    if (!process)
        return {};
    InodeMetadata metadata;
    metadata.inode = { fsid(), process->component_index() };
    metadata.mode = S_IFDIR | process->required_mode();
    metadata.uid = process->owner_user();
    metadata.gid = process->owner_group();
    metadata.size = 0;
    metadata.mtime = process->modified_time();
    return metadata;
}

KResultOr<size_t> ProcFSProcessDirectoryInode::read_bytes(off_t, size_t, UserOrKernelBuffer&, FileDescription*) const
{
    VERIFY_NOT_REACHED();
}

KResult ProcFSProcessDirectoryInode::traverse_as_directory(Function<bool(FileSystem::DirectoryEntryView const&)> callback) const
{
    MutexLocker locker(procfs().m_lock);
    auto process = Process::from_pid(associated_pid());
    if (!process)
        return EINVAL;
    return process->traverse_as_directory(procfs().fsid(), move(callback));
}

RefPtr<Inode> ProcFSProcessDirectoryInode::lookup(StringView name)
{
    MutexLocker locker(procfs().m_lock);
    auto process = Process::from_pid(associated_pid());
    if (!process)
        return nullptr;
    if (name == "fd")
        return ProcFSProcessSubDirectoryInode::create(procfs(), SegmentedProcFSIndex::ProcessSubDirectory::FileDescriptions, associated_pid());
    if (name == "stacks")
        return ProcFSProcessSubDirectoryInode::create(procfs(), SegmentedProcFSIndex::ProcessSubDirectory::Stacks, associated_pid());
    if (name == "unveil")
        return ProcFSProcessPropertyInode::create_for_pid_property(procfs(), SegmentedProcFSIndex::MainProcessProperty::Unveil, associated_pid());
    if (name == "pledge")
        return ProcFSProcessPropertyInode::create_for_pid_property(procfs(), SegmentedProcFSIndex::MainProcessProperty::Pledge, associated_pid());
    if (name == "fds")
        return ProcFSProcessPropertyInode::create_for_pid_property(procfs(), SegmentedProcFSIndex::MainProcessProperty::FileDescriptions, associated_pid());
    if (name == "exe")
        return ProcFSProcessPropertyInode::create_for_pid_property(procfs(), SegmentedProcFSIndex::MainProcessProperty::BinaryLink, associated_pid());
    if (name == "cwd")
        return ProcFSProcessPropertyInode::create_for_pid_property(procfs(), SegmentedProcFSIndex::MainProcessProperty::CurrentWorkDirectoryLink, associated_pid());
    if (name == "perf_events")
        return ProcFSProcessPropertyInode::create_for_pid_property(procfs(), SegmentedProcFSIndex::MainProcessProperty::PerformanceEvents, associated_pid());
    if (name == "vm")
        return ProcFSProcessPropertyInode::create_for_pid_property(procfs(), SegmentedProcFSIndex::MainProcessProperty::VirtualMemoryStats, associated_pid());
    if (name == "root")
        return ProcFSProcessPropertyInode::create_for_pid_property(procfs(), SegmentedProcFSIndex::MainProcessProperty::RootLink, associated_pid());
    return nullptr;
}

NonnullRefPtr<ProcFSProcessSubDirectoryInode> ProcFSProcessSubDirectoryInode::create(const ProcFS& procfs, SegmentedProcFSIndex::ProcessSubDirectory sub_directory_type, ProcessID pid)
{
    return adopt_ref_if_nonnull(new (nothrow) ProcFSProcessSubDirectoryInode(procfs, sub_directory_type, pid)).release_nonnull();
}

ProcFSProcessSubDirectoryInode::ProcFSProcessSubDirectoryInode(const ProcFS& procfs, SegmentedProcFSIndex::ProcessSubDirectory sub_directory_type, ProcessID pid)
    : ProcFSProcessAssociatedInode(procfs, pid, SegmentedProcFSIndex::build_segmented_index_for_sub_directory(pid, sub_directory_type))
    , m_sub_directory_type(sub_directory_type)
{
}

KResultOr<size_t> ProcFSProcessSubDirectoryInode::read_bytes(off_t, size_t, UserOrKernelBuffer&, FileDescription*) const
{
    VERIFY_NOT_REACHED();
}

KResult ProcFSProcessSubDirectoryInode::attach(FileDescription&)
{
    return KSuccess;
}

void ProcFSProcessSubDirectoryInode::did_seek(FileDescription&, off_t)
{
    VERIFY_NOT_REACHED();
}

InodeMetadata ProcFSProcessSubDirectoryInode::metadata() const
{
    MutexLocker locker(m_inode_lock);
    auto process = Process::from_pid(associated_pid());
    if (!process)
        return {};
    InodeMetadata metadata;
    metadata.inode = { fsid(), process->component_index() };
    metadata.mode = S_IFDIR | process->required_mode();
    metadata.uid = process->owner_user();
    metadata.gid = process->owner_group();
    metadata.size = 0;
    metadata.mtime = process->modified_time();
    return metadata;
}

KResult ProcFSProcessSubDirectoryInode::traverse_as_directory(Function<bool(FileSystem::DirectoryEntryView const&)> callback) const
{
    MutexLocker locker(procfs().m_lock);
    auto process = Process::from_pid(associated_pid());
    if (!process)
        return EINVAL;
    switch (m_sub_directory_type) {
    case SegmentedProcFSIndex::ProcessSubDirectory::FileDescriptions:
        return process->traverse_file_descriptions_directory(procfs().fsid(), move(callback));
    case SegmentedProcFSIndex::ProcessSubDirectory::Stacks:
        return process->traverse_stacks_directory(procfs().fsid(), move(callback));
    default:
        VERIFY_NOT_REACHED();
    }
    VERIFY_NOT_REACHED();
}

RefPtr<Inode> ProcFSProcessSubDirectoryInode::lookup(StringView name)
{
    MutexLocker locker(procfs().m_lock);
    auto process = Process::from_pid(associated_pid());
    if (!process)
        return {};
    switch (m_sub_directory_type) {
    case SegmentedProcFSIndex::ProcessSubDirectory::FileDescriptions:
        return process->lookup_file_descriptions_directory(procfs(), name);
    case SegmentedProcFSIndex::ProcessSubDirectory::Stacks:
        return process->lookup_stacks_directory(procfs(), name);
    default:
        VERIFY_NOT_REACHED();
    }
    VERIFY_NOT_REACHED();
}

NonnullRefPtr<ProcFSProcessPropertyInode> ProcFSProcessPropertyInode::create_for_file_description_link(const ProcFS& procfs, unsigned file_description_index, ProcessID pid)
{
    return adopt_ref_if_nonnull(new (nothrow) ProcFSProcessPropertyInode(procfs, file_description_index, pid)).release_nonnull();
}
NonnullRefPtr<ProcFSProcessPropertyInode> ProcFSProcessPropertyInode::create_for_thread_stack(const ProcFS& procfs, ThreadID stack_thread_index, ProcessID pid)
{
    return adopt_ref_if_nonnull(new (nothrow) ProcFSProcessPropertyInode(procfs, stack_thread_index, pid)).release_nonnull();
}
NonnullRefPtr<ProcFSProcessPropertyInode> ProcFSProcessPropertyInode::create_for_pid_property(const ProcFS& procfs, SegmentedProcFSIndex::MainProcessProperty main_property_type, ProcessID pid)
{
    return adopt_ref_if_nonnull(new (nothrow) ProcFSProcessPropertyInode(procfs, main_property_type, pid)).release_nonnull();
}

ProcFSProcessPropertyInode::ProcFSProcessPropertyInode(const ProcFS& procfs, SegmentedProcFSIndex::MainProcessProperty main_property_type, ProcessID pid)
    : ProcFSProcessAssociatedInode(procfs, pid, SegmentedProcFSIndex::build_segmented_index_for_main_property_in_pid_directory(pid, main_property_type))
    , m_parent_sub_directory_type(SegmentedProcFSIndex::ProcessSubDirectory::Reserved)
{
    m_possible_data.property_type = main_property_type;
}

ProcFSProcessPropertyInode::ProcFSProcessPropertyInode(const ProcFS& procfs, unsigned file_description_index, ProcessID pid)
    : ProcFSProcessAssociatedInode(procfs, pid, SegmentedProcFSIndex::build_segmented_index_for_file_description(pid, file_description_index))
    , m_parent_sub_directory_type(SegmentedProcFSIndex::ProcessSubDirectory::FileDescriptions)
{
    m_possible_data.property_index = file_description_index;
}

ProcFSProcessPropertyInode::ProcFSProcessPropertyInode(const ProcFS& procfs, ThreadID thread_stack_index, ProcessID pid)
    : ProcFSProcessAssociatedInode(procfs, pid, SegmentedProcFSIndex::build_segmented_index_for_thread_stack(pid, thread_stack_index))
    , m_parent_sub_directory_type(SegmentedProcFSIndex::ProcessSubDirectory::Stacks)
{
    m_possible_data.property_index = thread_stack_index.value();
}

KResult ProcFSProcessPropertyInode::attach(FileDescription& description)
{
    return refresh_data(description);
}
void ProcFSProcessPropertyInode::did_seek(FileDescription& description, off_t offset)
{
    if (offset != 0)
        return;
    (void)refresh_data(description);
}

static mode_t determine_procfs_process_inode_mode(SegmentedProcFSIndex::ProcessSubDirectory parent_sub_directory_type, SegmentedProcFSIndex::MainProcessProperty main_property)
{
    if (parent_sub_directory_type == SegmentedProcFSIndex::ProcessSubDirectory::FileDescriptions)
        return S_IFLNK | 0400;
    if (parent_sub_directory_type == SegmentedProcFSIndex::ProcessSubDirectory::Stacks)
        return S_IFREG | 0400;
    VERIFY(parent_sub_directory_type == SegmentedProcFSIndex::ProcessSubDirectory::Reserved);
    if (main_property == SegmentedProcFSIndex::MainProcessProperty::BinaryLink)
        return S_IFLNK | 0777;
    if (main_property == SegmentedProcFSIndex::MainProcessProperty::CurrentWorkDirectoryLink)
        return S_IFLNK | 0777;
    if (main_property == SegmentedProcFSIndex::MainProcessProperty::RootLink)
        return S_IFLNK | 0777;
    return S_IFREG | 0400;
}

InodeMetadata ProcFSProcessPropertyInode::metadata() const
{
    MutexLocker locker(m_inode_lock);
    auto process = Process::from_pid(associated_pid());
    if (!process)
        return {};
    InodeMetadata metadata;
    metadata.inode = { fsid(), process->component_index() };
    metadata.mode = determine_procfs_process_inode_mode(m_parent_sub_directory_type, m_possible_data.property_type);
    metadata.uid = process->owner_user();
    metadata.gid = process->owner_group();
    metadata.size = 0;
    metadata.mtime = process->modified_time();
    return metadata;
}
KResult ProcFSProcessPropertyInode::traverse_as_directory(Function<bool(FileSystem::DirectoryEntryView const&)>) const
{
    VERIFY_NOT_REACHED();
}
KResultOr<size_t> ProcFSProcessPropertyInode::read_bytes(off_t offset, size_t count, UserOrKernelBuffer& buffer, FileDescription* description) const
{
    dbgln_if(PROCFS_DEBUG, "ProcFS ProcessInformation: read_bytes offset: {} count: {}", offset, count);

    VERIFY(offset >= 0);
    VERIFY(buffer.user_or_kernel_ptr());

    if (!description) {
        KBufferBuilder builder;
        auto process = Process::from_pid(associated_pid());
        if (!process)
            return KResult(ESRCH);
        if (auto result = try_to_acquire_data(*process, builder); result.is_error())
            return result.error();
        auto data_buffer = builder.build();
        if (!data_buffer)
            return KResult(EFAULT);
        ssize_t nread = min(static_cast<off_t>(data_buffer->size() - offset), static_cast<off_t>(count));
        if (!buffer.write(data_buffer->data() + offset, nread))
            return KResult(EFAULT);
        return nread;
    }
    if (!description->data()) {
        dbgln("ProcFS Process Information: Do not have cached data!");
        return KResult(EIO);
    }

    MutexLocker locker(m_refresh_lock);

    auto& typed_cached_data = static_cast<ProcFSInodeData&>(*description->data());
    auto& data_buffer = typed_cached_data.buffer;

    if (!data_buffer || (size_t)offset >= data_buffer->size())
        return 0;

    ssize_t nread = min(static_cast<off_t>(data_buffer->size() - offset), static_cast<off_t>(count));
    if (!buffer.write(data_buffer->data() + offset, nread))
        return KResult(EFAULT);

    return nread;
}
RefPtr<Inode> ProcFSProcessPropertyInode::lookup(StringView)
{
    VERIFY_NOT_REACHED();
}

static KResult build_from_cached_data(KBufferBuilder& builder, ProcFSInodeData& cached_data)
{
    cached_data.buffer = builder.build();
    if (!cached_data.buffer)
        return ENOMEM;
    return KSuccess;
}

KResult ProcFSProcessPropertyInode::try_to_acquire_data(Process& process, KBufferBuilder& builder) const
{
    // FIXME: Verify process is already ref-counted
    if (m_parent_sub_directory_type == SegmentedProcFSIndex::ProcessSubDirectory::FileDescriptions) {
        if (auto result = process.procfs_get_file_description_link(m_possible_data.property_index, builder); result.is_error())
            return result.error();
        return KSuccess;
    }
    if (m_parent_sub_directory_type == SegmentedProcFSIndex::ProcessSubDirectory::Stacks) {
        if (auto result = process.procfs_get_thread_stack(m_possible_data.property_index, builder); result.is_error())
            return result.error();
        return KSuccess;
    }

    VERIFY(m_parent_sub_directory_type == SegmentedProcFSIndex::ProcessSubDirectory::Reserved);
    switch (m_possible_data.property_type) {
    case SegmentedProcFSIndex::MainProcessProperty::Unveil:
        return process.procfs_get_fds_stats(builder);
    case SegmentedProcFSIndex::MainProcessProperty::Pledge:
        return process.procfs_get_pledge_stats(builder);
    case SegmentedProcFSIndex::MainProcessProperty::FileDescriptions:
        return process.procfs_get_fds_stats(builder);
    case SegmentedProcFSIndex::MainProcessProperty::BinaryLink:
        return process.procfs_get_binary_link(builder);
    case SegmentedProcFSIndex::MainProcessProperty::CurrentWorkDirectoryLink:
        return process.procfs_get_current_work_directory_link(builder);
    case SegmentedProcFSIndex::MainProcessProperty::PerformanceEvents:
        return process.procfs_get_perf_events(builder);
    case SegmentedProcFSIndex::MainProcessProperty::VirtualMemoryStats:
        return process.procfs_get_virtual_memory_stats(builder);
    case SegmentedProcFSIndex::MainProcessProperty::RootLink:
        return process.procfs_get_root_link(builder);
    default:
        VERIFY_NOT_REACHED();
    }
}

KResult ProcFSProcessPropertyInode::refresh_data(FileDescription& description)
{
    // For process-specific inodes, hold the process's ptrace lock across refresh
    // and refuse to load data if the process is not dumpable.
    // Without this, files opened before a process went non-dumpable could still be used for dumping.
    auto process = Process::from_pid(associated_pid());
    if (!process)
        return KResult(ESRCH);
    process->ptrace_lock().lock();
    if (!process->is_dumpable()) {
        process->ptrace_lock().unlock();
        return EPERM;
    }
    ScopeGuard guard = [&] {
        process->ptrace_lock().unlock();
    };
    MutexLocker locker(m_refresh_lock);
    auto& cached_data = description.data();
    if (!cached_data) {
        cached_data = adopt_own_if_nonnull(new (nothrow) ProcFSInodeData);
        if (!cached_data)
            return ENOMEM;
    }
    KBufferBuilder builder;
    if (auto result = try_to_acquire_data(*process, builder); result.is_error())
        return result;
    return build_from_cached_data(builder, static_cast<ProcFSInodeData&>(*cached_data));
}
}
