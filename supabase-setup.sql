-- Run once in Supabase: Dashboard > SQL Editor > New query > paste > Run.
-- Counts one row per downloadable file; the public (anon) key can only call
-- increment_download(), it cannot read, insert or edit rows directly.

create table if not exists public.download_counts (
  file_path text primary key,
  count bigint not null default 0
);

alter table public.download_counts enable row level security;  -- no policies = no direct access

insert into public.download_counts (file_path) values
  ('downloads/results-sheet.pdf'),
  ('downloads/physical-test.pdf'),
  ('downloads/digital-test-screens.zip'),
  ('code/generate_print_cards.py'),
  ('code/generate_screens.py'),
  ('downloads/hueview-frame.zip'),
  ('downloads/3d/hueview-part-1.stl'),
  ('downloads/3d/hueview-part-2.stl'),
  ('downloads/3d/hueview-part-3.stl'),
  ('downloads/3d/hueview-part-4.stl'),
  ('downloads/3d/hueview-part-5.stl'),
  ('code/hueview.ino'),
  ('downloads/colorblindglasses.zip'),
  ('downloads/glasses/colorblindglasses-20.stl'),
  ('downloads/glasses/colorblindglasses-21.stl')
on conflict (file_path) do nothing;

create or replace function public.increment_download(p_file_path text)
returns void
language sql
security definer
set search_path = public
as $$
  update public.download_counts
  set count = count + 1
  where file_path = p_file_path;   -- only known files can be counted
$$;

revoke all on function public.increment_download(text) from public;
grant execute on function public.increment_download(text) to anon;
