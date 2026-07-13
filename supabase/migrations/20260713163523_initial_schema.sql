begin;

create table public.profiles (
    id uuid primary key references auth.users (id) on delete cascade,
    display_name text not null default 'mote user'
        check (char_length(display_name) between 1 and 80),
    created_at timestamptz not null default now()
);

create table public.settings (
    user_id uuid primary key references public.profiles (id) on delete cascade,
    assistant_name text not null default 'Mote'
        check (char_length(assistant_name) between 1 and 40),
    personality text not null default 'default'
        check (char_length(personality) between 1 and 80),
    voice text not null default 'default'
        check (char_length(voice) between 1 and 80),
    updated_at timestamptz not null default now()
);

create table public.memories (
    id uuid primary key default gen_random_uuid(),
    user_id uuid not null references public.profiles (id) on delete cascade,
    memory text not null
        check (char_length(memory) between 1 and 1000),
    importance smallint not null default 3
        check (importance between 1 and 5),
    created_at timestamptz not null default now()
);

create index memories_user_created_at_idx
    on public.memories (user_id, created_at desc);

create or replace function public.set_updated_at()
returns trigger
language plpgsql
security invoker
set search_path = ''
as $$
begin
    new.updated_at = now();
    return new;
end;
$$;

create trigger settings_set_updated_at
before update on public.settings
for each row
execute function public.set_updated_at();

create or replace function public.handle_new_user()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
    requested_name text;
begin
    requested_name := nullif(trim(new.raw_user_meta_data ->> 'display_name'), '');

    insert into public.profiles (id, display_name)
    values (new.id, left(coalesce(requested_name, 'mote user'), 80));

    insert into public.settings (user_id)
    values (new.id);

    return new;
end;
$$;

create trigger on_auth_user_created
after insert on auth.users
for each row
execute function public.handle_new_user();

insert into public.profiles (id, display_name, created_at)
select
    users.id,
    left(
        coalesce(nullif(trim(users.raw_user_meta_data ->> 'display_name'), ''), 'mote user'),
        80
    ),
    users.created_at
from auth.users as users
on conflict (id) do nothing;

insert into public.settings (user_id)
select profiles.id
from public.profiles as profiles
on conflict (user_id) do nothing;

alter table public.profiles enable row level security;
alter table public.settings enable row level security;
alter table public.memories enable row level security;

create policy "profiles_select_own"
on public.profiles
for select
to authenticated
using ((select auth.uid()) = id);

create policy "profiles_insert_own"
on public.profiles
for insert
to authenticated
with check ((select auth.uid()) = id);

create policy "profiles_update_own"
on public.profiles
for update
to authenticated
using ((select auth.uid()) = id)
with check ((select auth.uid()) = id);

create policy "profiles_delete_own"
on public.profiles
for delete
to authenticated
using ((select auth.uid()) = id);

create policy "settings_select_own"
on public.settings
for select
to authenticated
using ((select auth.uid()) = user_id);

create policy "settings_insert_own"
on public.settings
for insert
to authenticated
with check ((select auth.uid()) = user_id);

create policy "settings_update_own"
on public.settings
for update
to authenticated
using ((select auth.uid()) = user_id)
with check ((select auth.uid()) = user_id);

create policy "settings_delete_own"
on public.settings
for delete
to authenticated
using ((select auth.uid()) = user_id);

create policy "memories_select_own"
on public.memories
for select
to authenticated
using ((select auth.uid()) = user_id);

create policy "memories_insert_own"
on public.memories
for insert
to authenticated
with check ((select auth.uid()) = user_id);

create policy "memories_update_own"
on public.memories
for update
to authenticated
using ((select auth.uid()) = user_id)
with check ((select auth.uid()) = user_id);

create policy "memories_delete_own"
on public.memories
for delete
to authenticated
using ((select auth.uid()) = user_id);

revoke all on table public.profiles from anon;
revoke all on table public.settings from anon;
revoke all on table public.memories from anon;

grant select, insert, update, delete on table public.profiles to authenticated;
grant select, insert, update, delete on table public.settings to authenticated;
grant select, insert, update, delete on table public.memories to authenticated;

comment on table public.memories is
    'Explicitly retained user memories only; conversations are never stored here.';

commit;
